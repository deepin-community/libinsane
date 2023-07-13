#ifndef __LIBINSANE_DUMB_H
#define __LIBINSANE_DUMB_H

#include "capi.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIS_DUMB_DEV_ID_FORMAT "dumb dev%d"
#define LIS_DUMB_DEV_ID_FIRST "dumb dev0"

/*!
 * \brief Dumb implementation. Returns 0 scanners by default.
 * Only useful for testing. Used mostly in unit tests.
 * \param[out] impl will point to the Dumb implementation
 * \param[in] name name of the API
 */
extern enum lis_error lis_api_dumb(struct lis_api **impl, const char *name);

void lis_dumb_set_dev_descs(struct lis_api *impl, struct lis_device_descriptor **descs);

/**
 * \brief generate fake device (and device descriptors)
 */
void lis_dumb_set_nb_devices(struct lis_api *self, int nb_devices);
void lis_dumb_set_nb_devices_with_type(
		struct lis_api *self, int nb_devices, enum lis_item_type item_type
	);
void lis_dumb_set_list_devices_return(struct lis_api *self, enum lis_error ret);
void lis_dumb_set_get_device_return(struct lis_api *self, enum lis_error ret);

void lis_dumb_add_option(struct lis_api *self, const struct lis_option_descriptor *opt,
	const union lis_value *default_value, int set_flags);

struct lis_dumb_read {
	const void *content;
	size_t nb_bytes;
};

void lis_dumb_set_scan_parameters(
	struct lis_api *self, const struct lis_scan_parameters *params
);
void lis_dumb_set_scan_result(
	struct lis_api *self, const struct lis_dumb_read *read_contents,
	int nb_reads
);

void lis_dumb_reset_counters(struct lis_api *self);
int lis_dumb_get_nb_get(struct lis_api *self);
int lis_dumb_get_nb_set(struct lis_api *self);
int lis_dumb_get_nb_list_options(struct lis_api *self);


#ifdef __cplusplus
}
#endif

#endif
