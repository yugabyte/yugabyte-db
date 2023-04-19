#ifndef OD_DEBUGPRINTF_H
#define OD_DEBUGPRINTF_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

void od_dbg_printf(char *fmt, ...);

#define OD_RELEASE_MODE -1

#ifndef OD_DEVEL_LVL
/* set "release" mode by default */
#define OD_DEVEL_LVL OD_RELEASE_MODE
#endif

#if OD_DEVEL_LVL == OD_RELEASE_MODE
#define od_dbg_printf_on_dvl_lvl(debug_lvl, fmt, ...)
/* zero cost debug print on release mode */
#else
#define od_dbg_printf_on_dvl_lvl(debug_lvl, fmt, ...) \
                                                      \
	if (OD_DEVEL_LVL >= debug_lvl) {              \
		od_dbg_printf(fmt, __VA_ARGS__);      \
	}
#endif

#endif /* OD_DEBUGPRINTF_H */
