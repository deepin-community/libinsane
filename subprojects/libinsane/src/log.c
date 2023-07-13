#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libinsane/log.h>


static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;


static const struct lis_log_callbacks g_default_callbacks = {
	.callbacks = {
		[LIS_LOG_LVL_DEBUG] = lis_log_stderr,
		[LIS_LOG_LVL_INFO] = lis_log_stderr,
		[LIS_LOG_LVL_WARNING] = lis_log_stderr,
		[LIS_LOG_LVL_ERROR] = lis_log_stderr,
	}
};


static const struct lis_log_callbacks *g_current_callbacks = &g_default_callbacks;


void lis_set_log_callbacks(const struct lis_log_callbacks *callbacks)
{
	if (callbacks == NULL) {
		callbacks = &g_default_callbacks;
	}
	g_current_callbacks = callbacks;
}


static inline void lis_log_std(FILE* stream, enum lis_log_level lis_lvl, const char *msg)
{
	const char *lvl = "UNKNOWN";

	switch(lis_lvl)
	{
		case LIS_LOG_LVL_DEBUG:
			lvl = "DEBUG";
			break;
		case LIS_LOG_LVL_INFO:
			lvl = "INFO";
			break;
		case LIS_LOG_LVL_WARNING:
			lvl = "WARNING";
			break;
		case LIS_LOG_LVL_ERROR:
			lvl = "ERROR";
			break;
	}

	fprintf(stream, "[LibInsane:%s] %s\n", lvl, msg);
}


void lis_log_stdout(enum lis_log_level lis_lvl, const char *msg)
{
	lis_log_std(stdout, lis_lvl, msg);
}


void lis_log_stderr(enum lis_log_level lis_lvl, const char *msg)
{
	lis_log_std(stderr, lis_lvl, msg);
}


void lis_log_raw(enum lis_log_level lvl, const char *msg)
{
	g_current_callbacks->callbacks[lvl](lvl, msg);
}


void lis_log(
		enum lis_log_level lvl, const char *file, int line, const char *func,
		const char *msg, ...
	)
{
	static char g_buffer[2048];
	int r;
	va_list ap;

	r = pthread_mutex_lock(&g_mutex);
	assert(r == 0);

	assert(lvl >= LIS_LOG_LVL_MIN);
	assert(lvl <= LIS_LOG_LVL_MAX);

	if (g_current_callbacks->callbacks[lvl] == NULL) {
		return;
	}

	r = snprintf(g_buffer, sizeof(g_buffer), "%s:L%d(%s): ", file, line, func);

	va_start(ap, msg);
	r = vsnprintf(g_buffer + r, sizeof(g_buffer) - r, msg, ap);
	va_end(ap);

	if (r < 0) {
		fprintf(stderr, "Failed to format log output: %d, %s", errno, strerror(errno));
		return;
	}

	for (r = 0 ; g_buffer[r] != '\0' ; r++) {
		/* no line return allowed */
		if (g_buffer[r] == '\n' || g_buffer[r] == '\r') {
			g_buffer[r] = '_';
		}
	}

	g_current_callbacks->callbacks[lvl](lvl, g_buffer);

	r = pthread_mutex_unlock(&g_mutex);
	assert(r == 0);
}


void lis_log_reset(void)
{
	g_current_callbacks = &g_default_callbacks;
}
