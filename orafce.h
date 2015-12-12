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

extern int ora_instr(text *txt, text *pattern, int start, int nth);
extern int ora_mb_strlen(text *str, char **sizes, int **positions);
extern int ora_mb_strlen1(text *str);

extern char *nls_date_format;
extern char *orafce_timezone;

/*
 * Version compatibility
 */

#if PG_VERSION_NUM >= 80400
extern Oid	equality_oper_funcid(Oid argtype);
#endif

/*
 * Date utils
 */
#if PG_VERSION_NUM >= 90400
#define STRING_PTR_FIELD_TYPE const char *const
#else
#define STRING_PTR_FIELD_TYPE char *
#endif

extern STRING_PTR_FIELD_TYPE ora_days[];

extern int ora_seq_search(const char *name, STRING_PTR_FIELD_TYPE array[], int max);

#endif
