/*
 * I need identification of Pg version for diff API fce 
 * construct_md_array
 */

#include "catalog/catversion.h"

#if (CATALOG_VERSION_NO <= 200211021)
#define PG_VERSION_73_COMPAT
#elif (CATALOG_VERSION_NO <= 200310211)
#define PG_VERSION_74_COMPAT
#elif (CATALOG_VERSION_NO <= 200422041)
#define PG_VERSION_80_COMPAT
#elif (CATALOG_VERSION_NO <= 200511171) 
#define PG_VERSION_81_COMPAT
#else
#define PG_VERSION_82_COMPAT
#endif
