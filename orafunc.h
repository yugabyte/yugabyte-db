#ifndef __ORAFUNC__
#define __ORAFUNC__

#include "postgres.h"
#include "catalog/catversion.h"
#include "nodes/pg_list.h"
#include <sys/time.h>
#include "utils/datetime.h"

#if (CATALOG_VERSION_NO <= 200211021)
#error PostgreSQL 7.3.x isn't supported
#define PG_VERSION_73_COMPAT
#elif (CATALOG_VERSION_NO <= 200310211)
#define PG_VERSION_74_COMPAT
#elif (CATALOG_VERSION_NO <= 200422041)
#define PG_VERSION_80_COMPAT
#elif (CATALOG_VERSION_NO <= 200511171) 
#define PG_VERSION_81_COMPAT
#elif (CATALOG_VERSION_NO <= 200611241)
#define PG_VERSION_82_COMPAT
#else
#define PG_VERSION_83_COMPAT
#endif

text* ora_substr(text *str, int start, int len, bool valid_length);
text* ora_make_text(char *c);
text* ora_make_text_fix(char *c, int n);
int   ora_instr(text *txt, text *pattern, int start, int nth);
text* ora_clone_text(text *t);
int ora_mb_strlen(text *str, char **sizes, int **positions);
int ora_mb_strlen1(text *str);

#ifndef SET_VARSIZE
#define SET_VARSIZE(x,len)	VARATT_SIZEP(x) = (len)
#endif

/* 7.4 doesn't know RAISE_EXCEPTION */
#ifndef ERRCODE_RAISE_EXCEPTION
#define ERRCODE_RAISE_EXCEPTION MAKE_SQLSTATE('P','0','0','0','1')
#endif

#ifndef InitFunctionCallInfoData
#define InitFunctionCallInfoData(Fcinfo, Flinfo, Nargs, Context, Resultinfo) \
        do { \
                (Fcinfo).flinfo = (Flinfo); \
                (Fcinfo).context = (Context); \
                (Fcinfo).resultinfo = (Resultinfo); \
                (Fcinfo).isnull = false; \
                (Fcinfo).nargs = (Nargs); \
        } while (0)
#endif

#ifndef list_make1
#define list_make1(x1)		makeList1(x1)
#define list_length(l) 		length(l)
#define	list_nth(list,n)	nth(n,list)
#define ListCell	List
#endif

/* 8.1. has not defined no_data_found */
#ifndef ERRCODE_NO_DATA_FOUND
#define ERRCODE_NO_DATA_FOUND MAKE_SQLSTATE('P','0','0','0','2') 
#endif

#ifndef SECS_PER_DAY
#define SECS_PER_DAY	86400
#endif

#ifndef USECS_PER_DAY
#define USECS_PER_DAY	INT64CONST(86400000000)
#endif

#ifdef PG_VERSION_74_COMPAT
#define pg_tm	tm
unsigned char ora_toupper(unsigned char ch);
unsigned char ora_tolower(unsigned char ch);
#define pg_toupper(c) ora_toupper(c)
#define pg_tolower(c) ora_tolower(c)
void ora_usleep(long microsec);
#define pg_usleep(t)	ora_usleep(t)
#define _pg_mblen(c) 			pg_mblen(((unsigned char *) c))
#define _pg_mbstrlen_with_len(buf, loc) pg_mbstrlen_with_len((unsigned char *)buf,loc)
#define _(x)				gettext(x)
TimestampTz ora_GetCurrentTimestamp(void);
#define _GetCurrentTimestamp()		ora_GetCurrentTimestamp()


#endif


#endif
