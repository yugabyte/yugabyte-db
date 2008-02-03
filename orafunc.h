#ifndef __ORAFUNC__
#define __ORAFUNC__

#include "postgres.h"
#include "catalog/catversion.h"
#include "nodes/pg_list.h"
#include <sys/time.h>
#include "utils/datetime.h"
#include "utils/datum.h"

#define TextPGetCString(t) \
        DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(t))) 
#define CStringGetTextP(c) \
        DatumGetTextP(DirectFunctionCall1(textin, CStringGetDatum(c)))

#define TextPCopy(t) \
	DatumGetTextP(datumCopy(PointerGetDatum(t), false, -1))

text* ora_substr(text *str, int start, int len, bool valid_length);
text* ora_make_text_fix(char *c, int n);
int   ora_instr(text *txt, text *pattern, int start, int nth);
int ora_mb_strlen(text *str, char **sizes, int **positions);
int ora_mb_strlen1(text *str);

#endif
