#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libinsane/capi.h>
#include <libinsane/dumb.h>
#include <libinsane/error.h>
#include <libinsane/log.h>
#include <libinsane/util.h>

#define MAX_DUMB_OPTS 32


struct lis_dumb_option {
	struct lis_option_descriptor parent;
	struct lis_dumb_private *impl;
	int set_flags;

	int has_value;
	union lis_value default_value;
	union lis_value value;
};
#define LIS_DUMB_OPTION(opt) ((struct lis_dumb_option *)(opt))

struct lis_dumb_item {
	struct lis_item base;
	struct lis_dumb_private *impl;

	const char *dev_id;
};
#define LIS_DUMB_ITEM(item) ((struct lis_dumb_item *)(item))


struct lis_dumb_scan_session {
	struct lis_scan_session parent;
	struct lis_dumb_private *impl;
	int read_idx;
	int read_offset;
};
#define LIS_DUMB_SCAN_SESSION(scan_session) ((struct lis_dumb_scan_session *)(scan_session));


struct lis_dumb_private {
	struct lis_api base;

	enum lis_error list_devices_ret;
	struct lis_device_descriptor **descs;
	int allocated_descs;

	enum lis_error get_device_ret;
	struct lis_dumb_item **devices;

	struct lis_option_descriptor *opts[MAX_DUMB_OPTS + 1];

	struct lis_scan_parameters scan_parameters;

	struct {
		const struct lis_dumb_read *read_contents;
		int nb_reads;
		int is_scanning;
		struct lis_dumb_scan_session *session;
	} scan;

	struct {
		int list;
		int set;
		int get;
	} nb;
};
#define LIS_DUMB_PRIVATE(impl) ((struct lis_dumb_private *)(impl))


static enum lis_error dumb_get_scan_parameters(
	struct lis_scan_session *self, struct lis_scan_parameters *parameters
);
static int dumb_end_of_feed(struct lis_scan_session *session);
static int dumb_end_of_page(struct lis_scan_session *session);
static enum lis_error dumb_scan_read(
	struct lis_scan_session *session, void *out_buffer, size_t *buffer_size
);
static void dumb_cancel(struct lis_scan_session *session);


static struct lis_scan_session g_dumb_scan_session_template = {
	.get_scan_parameters = dumb_get_scan_parameters,
	.end_of_feed = dumb_end_of_feed,
	.end_of_page = dumb_end_of_page,
	.scan_read = dumb_scan_read,
	.cancel = dumb_cancel,
};


static enum lis_error dumb_get_children(struct lis_item *self, struct lis_item ***children);
static enum lis_error dumb_get_options(
	struct lis_item *self, struct lis_option_descriptor ***descs
);
static enum lis_error dumb_scan_start(struct lis_item *self, struct lis_scan_session **session);
static void dumb_close(struct lis_item *self);


static struct lis_item g_dumb_item_template = {
	.name = "dumb-o-jet",
	.type = LIS_ITEM_DEVICE,
	.get_children = dumb_get_children,
	.get_options = dumb_get_options,
	.scan_start = dumb_scan_start,
	.close = dumb_close,
};


static void dumb_cleanup(struct lis_api *impl);
static enum lis_error dumb_list_devices(
	struct lis_api *impl, enum lis_device_locations, struct lis_device_descriptor ***dev_infos
);
static enum lis_error dumb_get_device(
	struct lis_api *impl, const char *dev_id, struct lis_item **item
);

static struct lis_scan_parameters g_scan_parameters_template = {
	.format = LIS_IMG_FORMAT_RAW_RGB_24,
	.width = 256,
	.height = 256,
	.image_size = 256 * 256 * 3,
};


static struct lis_api g_dumb_api_template = {
	.base_name = NULL,
	.cleanup = dumb_cleanup,
	.list_devices = dumb_list_devices,
	.get_device = dumb_get_device,
};

static struct lis_device_descriptor *g_dumb_default_devices[] = { NULL };
static struct lis_item *g_dumb_default_children[] = { NULL };

static void dumb_cleanup_descs(struct lis_device_descriptor **descs)
{
	int i;

	if (descs == g_dumb_default_devices) {
		return;
	}

	for (i = 0 ; descs[i] != NULL ; i++) {
		free(descs[i]->dev_id);
		free(descs[i]);
	}

	free(descs);
}

static void dumb_cleanup_devices(struct lis_dumb_item **devs)
{
	int i;
	if (devs == NULL) {
		return;
	}

	for (i = 0 ; devs[i] != NULL ; i++) {
		free(devs[i]);
	}

	free(devs);
}


static void dumb_cleanup_opts(struct lis_dumb_private *private)
{
	int i;
	struct lis_dumb_option *opt_private;

	for (i = 0 ; private->opts[i] != NULL ; i++) {
		opt_private = LIS_DUMB_OPTION(private->opts[i]);
		lis_free(private->opts[i]->value.type, &opt_private->value);
		FREE(opt_private);
	}
}


static void dumb_cleanup(struct lis_api *self)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	free((void*)self->base_name);

	if (private->allocated_descs) {
		dumb_cleanup_descs(private->descs);
	}
	dumb_cleanup_devices(private->devices);
	dumb_cleanup_opts(private);
	free(private);
}


static enum lis_error dumb_list_devices(
		struct lis_api *self, enum lis_device_locations locations,
		struct lis_device_descriptor ***dev_infos
	)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);

	LIS_UNUSED(locations);

	if (LIS_IS_OK(private->list_devices_ret)) {
		*dev_infos = private->descs;
	} else {
		*dev_infos = NULL;
	}
	return private->list_devices_ret;
}

static enum lis_error dumb_get_device(
		struct lis_api *self, const char *dev_id, struct lis_item **item
	)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	int i;

	if (private->devices == NULL) {
		lis_log_error("[dumb] get_device() called when no device has been set"
				"; shouldn't happen");
		return LIS_ERR_INTERNAL_NOT_IMPLEMENTED;
	}

	if (LIS_IS_OK(private->get_device_ret)) {
		for (i = 0 ; private->devices[i] != NULL ; i++) {
			if (strcmp(dev_id, private->devices[i]->dev_id) == 0) {
				*item = &private->devices[i]->base;
				return LIS_OK;
			}
		}
		return LIS_ERR_INVALID_VALUE;
	} else {
		return private->get_device_ret;
	}
}


static enum lis_error dumb_get_children(struct lis_item *self, struct lis_item ***children)
{
	LIS_UNUSED(self);
	*children = g_dumb_default_children;
	return LIS_OK;
}


static enum lis_error dumb_opt_get_value(struct lis_option_descriptor *self, union lis_value *out_value)
{
	struct lis_dumb_option *private = LIS_DUMB_OPTION(self);

	private->impl->nb.get++;
	if (private->has_value) {
		memcpy(out_value, &private->value, sizeof(*out_value));
	} else {
		memcpy(out_value, &private->default_value, sizeof(*out_value));
	}
	return LIS_OK;
}


static enum lis_error dumb_opt_set_value(struct lis_option_descriptor *self,
		union lis_value value, int *set_flags)
{
	struct lis_dumb_option *private = LIS_DUMB_OPTION(self);

	private->impl->nb.set++;
	if (private->impl->scan.is_scanning) {
		return LIS_ERR_DEVICE_BUSY;
	}

	lis_copy(self->value.type, &value, &private->value);
	private->has_value = 1;

	*set_flags = private->set_flags;
	return LIS_OK;
}


static enum lis_error dumb_get_options(
		struct lis_item *self, struct lis_option_descriptor ***out_descs
	)
{
	struct lis_dumb_item *private = LIS_DUMB_ITEM(self);
	private->impl->nb.list++;
	*out_descs = private->impl->opts;
	return LIS_OK;
}


void lis_dumb_set_scan_parameters(struct lis_api *self,
	const struct lis_scan_parameters *params)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	memcpy(
		&private->scan_parameters, params,
		sizeof(private->scan_parameters)
	);
}

static enum lis_error dumb_get_scan_parameters(
		struct lis_scan_session *self,
		struct lis_scan_parameters *parameters
	)
{
	struct lis_dumb_scan_session *private = LIS_DUMB_SCAN_SESSION(self);
	memcpy(parameters, &private->impl->scan_parameters,
		sizeof(*parameters));
	return LIS_OK;
}


static enum lis_error dumb_scan_start(struct lis_item *self, struct lis_scan_session **out_session)
{
	struct lis_dumb_item *private = LIS_DUMB_ITEM(self);
	struct lis_dumb_scan_session *session;

	if (private->impl->scan.nb_reads <= 0) {
		lis_log_error("DUMB: Requested a scan, but tests haven't defined scan test output: %d",
				private->impl->scan.nb_reads);
		return LIS_ERR_JAMMED;
	}

	session = calloc(1, sizeof(struct lis_dumb_scan_session));
	memcpy(&session->parent, &g_dumb_scan_session_template, sizeof(session->parent));
	session->impl = private->impl;
	private->impl->scan.session = session;

	private->impl->scan.is_scanning = 1;
	*out_session = &session->parent;
	return LIS_OK;
}


static void dumb_close(struct lis_item *self)
{
	struct lis_dumb_item *private = LIS_DUMB_ITEM(self);
	FREE(private->impl->scan.session);
}


enum lis_error lis_api_dumb(struct lis_api **out_impl, const char *name)
{
	struct lis_dumb_private *private;

	private = calloc(1, sizeof(struct lis_dumb_private));
	memcpy(&private->base, &g_dumb_api_template, sizeof(private->base));
	memcpy(
		&private->scan_parameters, &g_scan_parameters_template,
		sizeof(private->scan_parameters)
	);
	private->base.base_name = strdup(name);

	private->list_devices_ret = LIS_OK;
	private->descs = g_dumb_default_devices;
	private->get_device_ret = LIS_OK;

	*out_impl = &private->base;
	return LIS_OK;
}


void lis_dumb_set_nb_devices_with_type(
		struct lis_api *self, int nb_devices, enum lis_item_type item_type
	)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	int i;
	struct lis_dumb_item *item;

	private->descs = calloc(nb_devices + 1, sizeof(struct lis_device_descriptor *));
	private->allocated_descs = 1;
	for (i = 0 ; i < nb_devices ; i++) {
		private->descs[i] = calloc(1, sizeof(struct lis_device_descriptor));
		if (asprintf(&private->descs[i]->dev_id, LIS_DUMB_DEV_ID_FORMAT, i) < 0) {
			// this is test code, so it doesn't actually matter.
			// but let's make GCC happy.
			assert(0);
		}
		private->descs[i]->vendor = "Microsoft";
		private->descs[i]->model = "Bugware";
		private->descs[i]->type = NULL;
	}

	private->devices = calloc(nb_devices + 1, sizeof(struct lis_dumb_item *));
	for (i = 0 ; i < nb_devices ; i++) {
		item = calloc(1, sizeof(struct lis_dumb_item));
		memcpy(&item->base, &g_dumb_item_template, sizeof(item->base));
		item->base.type = item_type;
		item->impl = private;
		item->dev_id = private->descs[i]->dev_id;
		private->devices[i] = item;
	}
}

void lis_dumb_set_nb_devices(struct lis_api *self, int nb_devices)
{
	lis_dumb_set_nb_devices_with_type(self, nb_devices, LIS_ITEM_UNIDENTIFIED);
}

void lis_dumb_set_dev_descs(struct lis_api *self, struct lis_device_descriptor **descs)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	private->descs = descs;
}


void lis_dumb_set_list_devices_return(struct lis_api *self, enum lis_error ret)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	private->list_devices_ret = ret;
}


void lis_dumb_set_get_device_return(struct lis_api *self, enum lis_error ret)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	private->get_device_ret = ret;
}


void lis_dumb_add_option(struct lis_api *self, const struct lis_option_descriptor *opt,
	const union lis_value *default_value, int set_flags)
{

	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	struct lis_dumb_option *opt_private;
	int i;

	opt_private = calloc(1, sizeof(struct lis_dumb_option));
	opt_private->impl = private;
	opt_private->set_flags = set_flags;
	memcpy(&opt_private->parent, opt, sizeof(opt_private->parent));
	if (opt_private->parent.fn.get_value == NULL) {
		opt_private->parent.fn.get_value = dumb_opt_get_value;
	}
	if (opt_private->parent.fn.set_value == NULL) {
		opt_private->parent.fn.set_value = dumb_opt_set_value;
	}
	memcpy(&opt_private->default_value, default_value, sizeof(opt_private->default_value));

	for (i = 0 ; i < MAX_DUMB_OPTS ; i++) {
		if (private->opts[i] == NULL || strcmp(private->opts[i]->name, opt->name) == 0) {
			break;
		}
	}
	assert(i < MAX_DUMB_OPTS);

	private->opts[i] = &opt_private->parent;
}


void lis_dumb_set_scan_result(struct lis_api *self, const struct lis_dumb_read *read_contents, int nb_reads)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	private->scan.read_contents = read_contents;
	private->scan.nb_reads = nb_reads;
}


static int dumb_end_of_feed(struct lis_scan_session *session)
{
	int r;
	struct lis_dumb_scan_session *private = LIS_DUMB_SCAN_SESSION(session);

	r = (private->read_idx >= private->impl->scan.nb_reads);
	if (r) {
		private->impl->scan.is_scanning = 0;
	}
	return r;
}


static int dumb_end_of_page(struct lis_scan_session *session)
{
	struct lis_dumb_scan_session *private = LIS_DUMB_SCAN_SESSION(session);

	if (private->read_idx >= private->impl->scan.nb_reads) {
		return 1;
	}

	if (private->impl->scan.read_contents[private->read_idx].nb_bytes == 0) {
		return 1;
	}

	if (dumb_end_of_feed(session)) {
		return 1;
	}
	return private->impl->scan.read_contents[private->read_idx].nb_bytes <= 0;
}


static enum lis_error dumb_scan_read(
		struct lis_scan_session *session, void *out_buffer, size_t *buffer_size
	)
{
	size_t max_read;
	struct lis_dumb_scan_session *private = LIS_DUMB_SCAN_SESSION(session);

	while(private->impl->scan.read_contents[private->read_idx].nb_bytes == 0) {
		private->read_idx++;
	}

	max_read = private->impl->scan.read_contents[private->read_idx].nb_bytes - private->read_offset;

	*buffer_size = MIN(*buffer_size, max_read);
	assert(*buffer_size > 0);

	memcpy(
		out_buffer,
		private->impl->scan.read_contents[private->read_idx].content + private->read_offset,
		*buffer_size
	);

	if (*buffer_size >= max_read) {
		private->read_idx++;
	} else {
		private->read_offset += *buffer_size;
	}
	return LIS_OK;
}


static void dumb_cancel(struct lis_scan_session *session)
{
	struct lis_dumb_private *impl;
	struct lis_dumb_scan_session *private = LIS_DUMB_SCAN_SESSION(session);

	impl = private->impl;

	private->read_idx = 0xFFFFFFFF;
	private->read_offset = 0xFFFFFFFF;
	FREE(impl->scan.session);
	impl->scan.is_scanning = 0;
}


void lis_dumb_reset_counters(struct lis_api *self)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	memset(&private->nb, 0, sizeof(private->nb));
}


int lis_dumb_get_nb_get(struct lis_api *self)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	return private->nb.get;
}


int lis_dumb_get_nb_set(struct lis_api *self)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	return private->nb.set;
}


int lis_dumb_get_nb_list_options(struct lis_api *self)
{
	struct lis_dumb_private *private = LIS_DUMB_PRIVATE(self);
	return private->nb.list;
}
