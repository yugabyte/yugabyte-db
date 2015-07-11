#ifndef __ORAFCE__
#define __ORAFCE__

#include "postgres.h"
#include "catalog/catversion.h"
#include "nodes/pg_list.h"
#include <sys/time.h>
#include "utils/datetime.h"
#include "utils/datum.h"

#define TextPCopy(t) \
	DatumGetTextP(datumCopy(PointerGetDatum(t), false, -1))

#define PG_GETARG_IF_EXISTS(n, type, defval) \
	((PG_NARGS() > (n) && !PG_ARGISNULL(n)) ? PG_GETARG_##type(n) : (defval))

/* alignment of this struct must fit for all types */
typedef union vardata
{
	char	c;
	short	s;
	int		i;
	long	l;
	float	f;
	double	d;
	void   *p;
} vardata;

int ora_instr(text *txt, text *pattern, int start, int nth);
int ora_mb_strlen(text *str, char **sizes, int **positions);
int ora_mb_strlen1(text *str);

/*
 * Version compatibility
 */

#if PG_VERSION_NUM >= 80400
extern Oid	equality_oper_funcid(Oid argtype);
#endif

#if PG_VERSION_NUM < 80400
#define CStringGetTextDatum(c) \
	DirectFunctionCall1(textin, CStringGetDatum(c))
#define text_to_cstring(t) \
	DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(t)))
#define cstring_to_text(c) \
	DatumGetTextP(CStringGetTextDatum(c))
text *cstring_to_text_with_len(const char *c, int n);
#define TextDatumGetCString(d) text_to_cstring((text *) DatumGetPointer(d))
#endif

#endif
