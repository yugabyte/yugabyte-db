#ifndef __ORAFUNC__
#define __ORAFUNC__

#include "postgres.h"
#include "catalog/catversion.h"
#include "nodes/pg_list.h"
#include <sys/time.h>
#include "utils/datetime.h"
#include "utils/datum.h"

#if PG_VERSION_NUM >= 80400
#define TextPGetCString(t)	text_to_cstring((t))
#define CStringGetTextP(c)	cstring_to_text((c))
#else
#define CStringGetTextDatum(c) \
        DirectFunctionCall1(textin, CStringGetDatum(c))
#define TextPGetCString(t) \
        DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(t))) 
#define CStringGetTextP(c) \
        DatumGetTextP(CStringGetTextDatum(c))
text *cstring_to_text_with_len(const char *c, int n);
#endif

#define TextPCopy(t) \
	DatumGetTextP(datumCopy(PointerGetDatum(t), false, -1))

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

#if PG_VERSION_NUM < 80300
#define PGDLLIMPORT				DLLIMPORT
#define session_timezone		global_timezone
#define DatumGetTextPP(p)		DatumGetTextP(p)
#define SET_VARSIZE(PTR, len)	(VARATT_SIZEP((PTR)) = (len))
#define PG_GETARG_TEXT_PP(n)	PG_GETARG_TEXT_P((n))
#define VARDATA_ANY(PTR)		VARDATA((PTR))
#define VARSIZE_ANY_EXHDR(PTR)	(VARSIZE((PTR)) - VARHDRSZ)
#define att_align_nominal(cur_offset, attalign) \
	att_align((cur_offset), (attalign))
#define att_addlength_pointer(cur_offset, attlen, attptr) \
	att_addlength((cur_offset), (attlen), (attptr))
#define stringToQualifiedNameList(string) \
	stringToQualifiedNameList((string), "")
typedef void *SPIPlanPtr;
#endif

#endif
