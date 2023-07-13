#include <stdio.h>
#include <stdlib.h>

#include <CUnit/Basic.h>

#include <libinsane/log.h>
#include <libinsane/util.h>

#include "main.h"


// #define LOG_DEBUG

#ifndef LOG_DEBUG
static void noop(enum lis_log_level lvl, const char *msg) {
	LIS_UNUSED(lvl);
	LIS_UNUSED(msg);
}
#endif

static const struct lis_log_callbacks g_log_callbacks = {
	.callbacks = {
#ifdef LOG_DEBUG
		[LIS_LOG_LVL_DEBUG] = lis_log_stdout,
		[LIS_LOG_LVL_INFO] = lis_log_stdout,
#else
		[LIS_LOG_LVL_DEBUG] = noop,
		[LIS_LOG_LVL_INFO] = noop,
#endif
		[LIS_LOG_LVL_WARNING] = lis_log_stdout,
		[LIS_LOG_LVL_ERROR] = lis_log_stdout,
	}
};

int main(int argc, char **argv)
{
	CU_ErrorCode err;
	int has_failed;

	LIS_UNUSED(argc);
	LIS_UNUSED(argv);

	lis_set_log_callbacks(&g_log_callbacks);

	if (CU_initialize_registry() != CUE_SUCCESS) {
		fprintf(stderr, "CU_initialize_registry() failed\n");
		return CU_get_error();
	}

	if (!register_tests()) {
		fprintf(stderr, "registring tests has failed\n");
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	has_failed = CU_get_number_of_failures();
	if (!has_failed && CU_get_number_of_tests_run() == 0) {
		has_failed = 1;
	}
	CU_cleanup_registry();
	err = CU_get_error();
	if (err != 0)
		return err;
	return (has_failed ? EXIT_FAILURE : EXIT_SUCCESS);
}
