#include "postgres.h"
#include "fmgr.h"


Datum ora_nvl(PG_FUNCTION_ARGS);
Datum ora_nvl2(PG_FUNCTION_ARGS);
Datum ora_concat(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(ora_concat);

Datum
ora_concat(PG_FUNCTION_ARGS)
{
    
    if (PG_ARGISNULL(0) && PG_ARGISNULL(1))
	PG_RETURN_NULL();
	
    if (PG_ARGISNULL(0))
	PG_RETURN_TEXT_P(PG_GETARG_TEXT_P(1));
	
    if (PG_ARGISNULL(1))
	PG_RETURN_TEXT_P(PG_GETARG_TEXT_P(0));

    text *t1 = PG_GETARG_TEXT_P(0);
    text *t2 = PG_GETARG_TEXT_P(1);    
    
    int l1 = VARSIZE(t1) - VARHDRSZ;
    int l2 = VARSIZE(t2) - VARHDRSZ;
    
    text *result = palloc(l1+l2+VARHDRSZ);
    memcpy(VARDATA(result), VARDATA(t1), l1);
    memcpy(VARDATA(result) + l1, VARDATA(t2), l2);
    VARATT_SIZEP(result) = l1 + l2 + VARHDRSZ;
    
    PG_RETURN_TEXT_P(result);
}


PG_FUNCTION_INFO_V1(ora_nvl);

Datum
ora_nvl(PG_FUNCTION_ARGS)
{

    if (!PG_ARGISNULL(0))
	PG_RETURN_DATUM(PG_GETARG_DATUM(0));

    if (!PG_ARGISNULL(1))
	PG_RETURN_DATUM(PG_GETARG_DATUM(1));

    PG_RETURN_NULL();
} 

PG_FUNCTION_INFO_V1(ora_nvl2);

Datum
ora_nvl2(PG_FUNCTION_ARGS)
{
    if (!PG_ARGISNULL(0))
	PG_RETURN_DATUM(PG_GETARG_DATUM(0));

    if (!PG_ARGISNULL(1))
	PG_RETURN_DATUM(PG_GETARG_DATUM(1));

    if (!PG_ARGISNULL(2))
	PG_RETURN_DATUM(PG_GETARG_DATUM(1));


    PG_RETURN_NULL();
} 

