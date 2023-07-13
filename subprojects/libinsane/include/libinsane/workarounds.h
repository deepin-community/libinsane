#ifndef __LIBINSANE_WORKAROUNDS_H
#define __LIBINSANE_WORKAROUNDS_H

#include "capi.h"


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Fix options names
 *
 * ## Option 'scan-resolution' --> 'resolution'
 *
 * - API: Sane
 * - Culprit: Lexmark
 * - Seen on: Lexmark MFP
 *
 * The option 'resolution' is mistakenly named 'scan-resolution'.
 * This workaround replaces it by an option 'resolution'.
 *
 * ## Option 'doc-source' --> 'source'
 *
 * - API: Sane
 * - Culprit: Samsung
 * - Seen on: [Samsung CLX-3300](https://openpaper.work/scanner_db/report/31/)
 *
 * The option 'source' is mistakenly named 'doc-source'.
 * This workaround replaces it by an option option 'source'.
 *
 * \param[in] to_wrap Base implementation to wrap.
 * \param[out] out_impl Implementation of the out_impl including the workaround.
 */
extern enum lis_error lis_api_workaround_opt_names(
	struct lis_api *to_wrap, struct lis_api **out_impl
);


/*!
 * \brief Replace unusual option values by usual ones
 *
 * ## Option 'mode': Unusual mode values
 *
 * - API: Sane
 * - Seen on:
 *   - [Brother MFC-7360N](https://openpaper.work/scanner_db/report/20/)
 *   - [Samsung CLX-3300](https://openpaper.work/scanner_db/report/31/)
 *   - [Brother ADS-2100e](https://openpaper.work/en/scanner_db/report/458/)
 *
 * Override the option 'mode' so it changes the following possible values:
 *
 * - Brother
 *   - '24bit Color' --> 'Color'
 *   - '24bit Color[Fast]' --> 'Color'
 *   - 'Black & White' --> 'LineArt'
 *   - 'True Gray' --> 'Gray'
 * - Samsung
 *   - 'Black and White - Line Art' --> 'LineArt'
 *   - 'Grayscale - 256 Levels' --> 'Gray'
 *   - 'Color - 16 Million Colors' --> 'Color'
 *
 * ## Strip option translations
 *
 * - API: Sane
 * - Culprits: Sane project, OKI
 * - Seen on: [OKI MC363](https://openpaper.work/scanner_db/report/56/)
 *
 * This workaround wraps a bunch of options, and try to revert the translations back to English.
 *
 * \param[in] to_wrap Base implementation to wrap.
 * \param[out] out_impl Implementation of the out_impl including the workaround.
 */
extern enum lis_error lis_api_workaround_opt_values(
	struct lis_api *to_wrap, struct lis_api **out_impl
);


/*!
 * \brief Prevent operations on options that are not allowed by capabilities
 *
 * ## Do not let application access value of inactive options
 *
 * - API: Sane
 * - Seen on: Sane test backend
 *
 * Some drivers allows access to inactive options (even just for reading).
 * Some may even crash if the user application tries to set a value on an inactive option.
 *
 * ## Do not let application set value on read-only options
 *
 * - API: Sane
 * - Seen on: Can't remember
 *
 * Behavior is undefined when trying to set read-only values.
 * This workaround makes it defined: it always returns an error.
 *
 * ##  Do not let application set value on option that can have only one value
 *
 * - API: Sane
 * - Seen on: [Epson DS-310](https://openpaper.work/scanner_db/report/120/)
 * - Seen on: Epson XP-425
 *
 * When trying to set a value on a property that accept only one value
 * (ex: source=ADF), Sane driver may return SANE_STATUS_INVAL instead of success.
 * This workaround makes sure the value provided matches the only one possible
 * and doesn't even set it.
 *
 * \param[in] to_wrap Base implementation to wrap.
 * \param[out] out_impl Implementation of the out_impl including the workaround.
 */
extern enum lis_error lis_api_workaround_check_capabilities(
	struct lis_api *to_wrap, struct lis_api **out_impl
);


#ifdef OS_LINUX
/*!
 * \brief Access scanners through a dedicated process.
 *
 * - API: Sane
 * - Culprit: Brother drivers, old HP drivers
 *
 * Sane backends run as part of the application that use them. However some of
 * them are unstable: some corrupt memory, some cause crashes, etc. Some appear
 * to work fine when running in simple applications (C + GTK, Python + shell,
 * etc) but do not anymore in more complex applications (Python + GTK, etc).
 * Since you probably don't want your application to crash or get its memory
 * corrupted, the only reliable workaround is to run the code that use Sane
 * and its backend in a dedicated process.
 *
 * WIA2 already has a dedicated process and therefore this workaround is not
 * required.
 */
extern enum lis_error lis_api_workaround_dedicated_process(
	struct lis_api *to_wrap, struct lis_api **out_impl
);
#endif

/*!
 * \brief Thread-safety
 *
 * - API: Sane, WIA, TWAIN
 *
 * Most scanner APIs are not thread-safe. If you're lucky, they may work
 * from different threads anyway. If you're not, they will crash your program.
 *
 * This workaround works around this issue by creating a dedicated thread for
 * the job and making all the request go through this thread.
 *
 * \param[in] to_wrap Base implementation to wrap.
 * \param[out] out_impl Implementation of the out_impl including the workaround.
 */
extern enum lis_error lis_api_workaround_dedicated_thread(
	struct lis_api *to_wrap, struct lis_api **out_impl
);


/*!
 * \brief Ensure Flatbeds return only one page.
 *
 * - API: Sane, WIA
 * - Culprit: EPSON XP-425 (Sane)
 *
 * Flatbed can only contain one single page. However when requesting
 * another page/image, some drivers keep saying "yeah sure no problem"
 * instead of "no more pages", and keep returning the same page/image
 * again and again.
 *
 * Requires normalizer 'normalizer_source_types'.
 *
 * \param[in] to_wrap Base implementation to wrap.
 * \param[out] out_impl Implementation of the out_impl including the workaround.
 */
extern enum lis_error lis_api_workaround_one_page_flatbed(
	struct lis_api *to_wrap, struct lis_api **out_impl
);


/*!
 * \brief Minimize calls to underlying API
 *
 * - API: Sane (maybe others)
 * - Culprit: HP drivers + sane backend 'net' (+ difference of versions
 *   between servers and clients) (maybe others)
 *
 * Some drivers or combinations of drivers seem to be very touchy. This
 * workaround aim to reduce to a strict minimum all the calls to
 * list_options(), option->fn.set(), option->fn.get().
 *
 * Assumes that the 'set_flags' when calling option->fn.set() is reliable.
 *
 * Also keep track of the items. Return the same items as long as
 * they haven't been closed. This reduce risk of programming error
 * (even more when using the GObject layer).
 */
extern enum lis_error lis_api_workaround_cache(
	struct lis_api *to_wrap, struct lis_api **out_impl
);


/*!
 * \brief Turns the lamp off at the end of the scan
 *
 * - API: Sane
 * - Culprit: Canon Lide 30 + Sane backend Plustek
 *
 * When scanning with the Canon Lide 30, the driver doesn't turn off the
 * lamp at the end of the scan. Without this workaround, the lamp remains
 * on until the scanner is powered down.
 */
extern enum lis_error lis_api_workaround_lamp(
	struct lis_api *to_wrap, struct lis_api **out_impl
);


#ifdef __cplusplus
}
#endif

#endif
