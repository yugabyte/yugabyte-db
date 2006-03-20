#ifndef __ORAFUNC__
#define __ORAFUNC__

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

text* ora_substr(text *str, int start, int len, bool valid_length);
text* ora_make_text(char *c);
text* ora_make_text_fix(char *c, int n);
int   ora_instr(text *txt, text *pattern, int start, int nth);
text* ora_clone_text(text *t);

#endif
