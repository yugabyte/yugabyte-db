/*
 This library implements Oracle DBMS_XSLPROCESSOR API. Some of code is based on
 J.Gray's xml2 contrib package. 
 
 Pavel Stehule
 
 history:
 
 May 2007 - initial version
*/

#include "postgres.h"
#include "fmgr.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "executor/tuptable.h"
#include "utils/lsyscache.h" 
#include "utils/typcache.h"


/* libxml includes */

#include <libxml/xpath.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

/* libxslt includes */

#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

#define MAXPROCS	12
#define GET_STR(textp) DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(textp)))


typedef struct aaaa {
    int handle;
} xslprocessor;

xslprocessor *proc_handlers[MAXPROCS];


PG_FUNCTION_INFO_V1(orafce_xsl_newProcessor);
PG_FUNCTION_INFO_V1(orafce_xsl_processXSL);
PG_FUNCTION_INFO_V1(orafce_xsl_showWarnings);
PG_FUNCTION_INFO_V1(orafce_xsl_setErrorLog);
PG_FUNCTION_INFO_V1(orafce_xsl_newStylesheet_DOM);
PG_FUNCTION_INFO_V1(orafce_xsl_newStylesheet);
PG_FUNCTION_INFO_V1(orafce_xsl_setParam);
PG_FUNCTION_INFO_V1(orafce_xsl_removeParam);
PG_FUNCTION_INFO_V1(orafce_xsl_resetParams);
PG_FUNCTION_INFO_V1(orafce_xsl_freestylesheet);
PG_FUNCTION_INFO_V1(orafce_xsl_freeProcessor);

Datum orafce_xsl_newProcessor(PG_FUNCTION_ARGS);
Datum orafce_xsl_processXSL(PG_FUNCTION_ARGS);
Datum orafce_xsl_showWarnings(PG_FUNCTION_ARGS);
Datum orafce_xsl_setErrorLog(PG_FUNCTION_ARGS);
Datum orafce_xsl_newStylesheet_DOM(PG_FUNCTION_ARGS);
Datum orafce_xsl_newStylesheet(PG_FUNCTION_ARGS);
Datum orafce_xsl_setParam(PG_FUNCTION_ARGS);
Datum orafce_xsl_removeParam(PG_FUNCTION_ARGS);
Datum orafce_xsl_resetParams(PG_FUNCTION_ARGS);
Datum orafce_xsl_freestylesheet(PG_FUNCTION_ARGS);
Datum orafce_xsl_freeProcessor(PG_FUNCTION_ARGS);

int32 getIdFromRecord(HeapTupleHeader rec);
HeapTuple BuildIdRec(int32 id, TupleDesc tupdesc);

/*

Datum
dbms_pipe_list_pipes (PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	TupleDesc        tupdesc;
	TupleTableSlot  *slot;
	AttInMetadata   *attinmeta;
	PipesFctx       *fctx;

	float8 endtime;
	int cycle = 0;
	int timeout = 10;	

	if (SRF_IS_FIRSTCALL ())
	{
		MemoryContext  oldcontext;
		bool has_lock = false;

		WATCH_PRE(timeout, endtime, cycle);	
		if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
		{
			has_lock = true;
			break;
		}
		WATCH_POST(timeout, endtime, cycle);
		if (!has_lock)
			LOCK_ERROR();

		funcctx = SRF_FIRSTCALL_INIT ();
		oldcontext = MemoryContextSwitchTo (funcctx->multi_call_memory_ctx);

		fctx = (PipesFctx*) palloc (sizeof (PipesFctx));
		funcctx->user_fctx = (void *)fctx;

		fctx->values = (char **) palloc (4 * sizeof (char *));
		fctx->values  [0] = (char*) palloc (255 * sizeof (char));
		fctx->values  [1] = (char*) palloc  (16 * sizeof (char));
		fctx->values  [2] = (char*) palloc  (16 * sizeof (char));
		fctx->values  [3] = (char*) palloc  (16 * sizeof (char));
		fctx->values  [4] = (char*) palloc  (10 * sizeof (char));
		fctx->values  [5] = (char*) palloc (255 * sizeof (char));
		fctx->pipe_nth = 0;

		tupdesc = CreateTemplateTupleDesc (6 , false);

#ifndef PG_VERSION_74_COMPAT 
		TupleDescInitEntry (tupdesc,  1, "Name",    VARCHAROID, -1, 0);
		TupleDescInitEntry (tupdesc,  2, "Items",   INT4OID   , -1, 0);
		TupleDescInitEntry (tupdesc,  3, "Size",    INT4OID,    -1, 0);
		TupleDescInitEntry (tupdesc,  4, "Limit",   INT4OID,    -1, 0);
		TupleDescInitEntry (tupdesc,  5, "Private", BOOLOID,    -1, 0);
		TupleDescInitEntry (tupdesc,  6, "Owner",   VARCHAROID, -1, 0);
#else
		TupleDescInitEntry (tupdesc,  1, "Name",    VARCHAROID, -1, 0, false);
		TupleDescInitEntry (tupdesc,  2, "Items",   INT4OID   , -1, 0, false);
		TupleDescInitEntry (tupdesc,  3, "Size",    INT4OID,    -1, 0, false);
		TupleDescInitEntry (tupdesc,  4, "Limit",   INT4OID,    -1, 0, false);
		TupleDescInitEntry (tupdesc,  5, "Private", BOOLOID,    -1, 0, false);
		TupleDescInitEntry (tupdesc,  6, "Owner",   VARCHAROID, -1, 0, false);
#endif
		
		slot = TupleDescGetSlot (tupdesc); 
		funcctx -> slot = slot;
		
		attinmeta = TupleDescGetAttInMetadata (tupdesc);
		funcctx -> attinmeta = attinmeta;
		
		MemoryContextSwitchTo (oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP ();
	fctx = (PipesFctx*) funcctx->user_fctx;

	while (fctx->pipe_nth < MAX_PIPES)
	{
		if (pipes[fctx->pipe_nth].is_valid)
		{
			Datum    result;
			char   **values;
			HeapTuple tuple;
			char     *aux_3, *aux_5;

			values = fctx->values;
			aux_3 = values[3]; aux_5 = values[5];
			values[3] = NULL; values[5] = NULL;

			snprintf (values[0], 255, "%s", pipes[fctx->pipe_nth].pipe_name);
			snprintf (values[1],  16, "%d", pipes[fctx->pipe_nth].count);
			snprintf (values[2],  16, "%d", pipes[fctx->pipe_nth].size);
			if (pipes[fctx->pipe_nth].limit != -1)
			{
				snprintf (aux_3,  16, "%d", pipes[fctx->pipe_nth].limit);
				values[3] = aux_3;

			}
			snprintf (values[4], 10, "%s", pipes[fctx->pipe_nth].creator != NULL ? "true" : "false");
			if (pipes[fctx->pipe_nth].creator != NULL)
			{
				snprintf (aux_5,  255, "%s", pipes[fctx->pipe_nth].creator);
				values[5] = aux_5;
			}
				
			tuple = BuildTupleFromCStrings (funcctx -> attinmeta,
											fctx -> values);
			result = TupleGetDatum (funcctx -> slot, tuple);
			
			values[3] = aux_3; values[5] = aux_5;
			fctx->pipe_nth += 1;
			SRF_RETURN_NEXT (funcctx, result);
		}
		fctx->pipe_nth += 1;
			
	}

	LWLockRelease(shmem_lock);	
	SRF_RETURN_DONE (funcctx);

*/




/*
 * Returns a new processor instance. This function must be called before the default behavior 
 * of Processor can be changed and if other processor methods need to be used.
 * Syntax
 *
 * FUNCTION newProcessor RETURN Processor;
 *
 * TYPE Processor IS RECORD (ID integer);
 * TYPE Stylesheet IS RECORD (ID integer);
 */
Datum
orafce_xsl_newProcessor(PG_FUNCTION_ARGS)
{
	TupleDesc       tupdesc;
	TupleDesc       btupdesc;
	HeapTupleHeader rec;

	int id =  getIdFromRecord(PG_GETARG_HEAPTUPLEHEADER(0));
                                                                                                                                                
	get_call_result_type(fcinfo, NULL, &tupdesc);
        btupdesc = BlessTupleDesc(tupdesc);

	rec = SPI_returntuple(BuildIdRec(++id, btupdesc), btupdesc);

	PG_RETURN_HEAPTUPLEHEADER(rec);	
}

/*
 * Return integer field "id" from record
 */
int32
getIdFromRecord(HeapTupleHeader rec)
{
        int32           tupTypmod;
        Oid                     tupType;
        TupleDesc       tupdesc;
        HeapTupleData tuple;
	Datum	   *values;
	char	   *nulls;
	int 	ncolumns;

	/* Extract type info from the tuple itself */
        tupType = HeapTupleHeaderGetTypeId(rec);
        tupTypmod = HeapTupleHeaderGetTypMod(rec);
        tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);
	ncolumns = tupdesc->natts;
	if (ncolumns != 1 || tupdesc->attrs[0]->atttypid != INT4OID)
		ereport(ERROR, 
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("Internal error"),
			 errdetail("used wrong record type")));

        /* Build a temporary HeapTuple control structure */
        tuple.t_len = HeapTupleHeaderGetDatumLength(rec);
        ItemPointerSetInvalid(&(tuple.t_self));
        tuple.t_tableOid = InvalidOid;
        tuple.t_data = rec;

        values = (Datum *) palloc(ncolumns * sizeof(Datum));
        nulls = (char *) palloc(ncolumns * sizeof(char));

        /* Break down the tuple into fields */
        heap_deformtuple(&tuple, tupdesc, values, nulls);

	ReleaseTupleDesc(tupdesc);

	return DatumGetInt32(values[0]);
}


HeapTuple
BuildIdRec(int32 id, TupleDesc tupdesc)
{
	Datum	values[1];
	char	nulls[1];

	values[0] = Int32GetDatum(id);
	nulls[0] = ' ';

	return heap_formtuple(tupdesc, values, nulls);
}



/* 
 * Transforms input XML document. Any changes to the default processor behavior should be effected 
 * before calling this procedure. An application error is raised if processing fails. The options 
 * are described in the following table.
 * Syntax 	Description
 * 
 * o Transforms input XML document using given DOMDocument and stylesheet, and returns the resultant document fragment.
 *     FUNCTION processXSL(p Processor, ss Stylesheet, xmldoc DOMDocument) RETURN DOMDocumentFragment;
 *
 * o Transforms input XML document using given document as URL and the stylesheet, and 
 *   returns the resultant document fragment.
 *     FUNCTION processXSL(p Processor, ss Stylesheet, url VARCHAR2) RETURN DOMDocumentFragment;
 *
 * o Transforms input XML document using given document as CLOB and the stylesheet, 
 *   and returns the resultant document fragment.
 *     FUNCTION processXSL(p Processor, ss Stylesheet, clb CLOB) RETURN DOMDocumentFragment;
 *
 * o Transforms input XML DocumentFragment using given DOMDocumentFragment and the stylesheet, 
 *   and writes the output to a CLOB. 
 *     PROCEDURE processXSL(p Processor, ss Stylesheet, xmldf DOMDocumentFragment, cl IN OUT CLOB);
 */
Datum
orafce_xsl_processXSL(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}					
					

/*
 * Turns warnings on (TRUE) or off (FALSE).
 * Syntax
 *
 * PROCEDURE showWarnings(p Processor, yes BOOLEAN);
 */
Datum
orafce_xsl_showWarnings(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


/*
 * Sets errors to be sent to the specified file.
 * Syntax
 *
 * PROCEDURE setErrorLog(p Processor, fileName VARCHAR2);
 */
Datum
orafce_xsl_setErrorLog(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}
 
 
/*
 * Creates and returns a new stylesheet instance. 
 * Syntax 
 * 
 * Creates and returns a new stylesheet instance using the given DOMDocument and reference URLs.
 * FUNCTION newStylesheet(xmldoc DOMDocument, ref VARCHAR2) RETURN Stylesheet;
 * 
 * Creates and returns a new stylesheet instance using the given input and reference URLs.
 * FUNCTION newStylesheet(inp VARCHAR2, ref VARCHAR2) RETURN Stylesheet;
 */
Datum
orafce_xsl_newStylesheet_DOM(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

Datum
orafce_xsl_newStylesheet(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}
	
	
/* 
 * Sets a top level parameter in the stylesheet. The parameter value must be a valid 
 * XPath expression. Literal string values must be quoted.
 * Syntax
 *
 * PROCEDURE setParam(ss Stylesheet, name VARCHAR2, value VARCHAR2);
 */
Datum
orafce_xsl_setParam(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


/*
 * Moves a top level stylesheet parameter.
 * Syntax
 *
 * PROCEDURE removeParam(ss Stylesheet, name VARCHAR2);
 */
Datum
orafce_xsl_removeParam(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}
                       
                       
/*
 * Resets the top-level stylesheet parameters.
 * Syntax
 *
 * PROCEDURE resetParams(ss Stylesheet);
 */
Datum
orafce_xsl_resetParams(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


/*
 * Frees a Stylesheet object.
 * Syntax
 *
 * PROCEDURE freestylesheet(ss Stylesheet);
 */
Datum
orafce_xsl_freestylesheet(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}
 

/*
 * Frees a Processor object.
 * Syntax
 *
 * PROCEDURE freeProccessor(p Processor);
 */
Datum
orafce_xsl_freeProcessor(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}






/*
Datum		xslt_process(PG_FUNCTION_ARGS);


#define MAXPARAMS 20

PG_FUNCTION_INFO_V1(xslt_process);

Datum
xslt_process(PG_FUNCTION_ARGS)
{


	const char *params[MAXPARAMS + 1];		
	xsltStylesheetPtr stylesheet = NULL;
	xmlDocPtr	doctree;
	xmlDocPtr	restree;
	xmlDocPtr	ssdoc = NULL;
	xmlChar    *resstr;
	int			resstat;
	int			reslen;

	text	   *doct = PG_GETARG_TEXT_P(0);
	text	   *ssheet = PG_GETARG_TEXT_P(1);
	text	   *paramstr;
	text	   *tres;


	if (fcinfo->nargs == 3)
	{
		paramstr = PG_GETARG_TEXT_P(2);
		parse_params(params, paramstr);
	}
	else
		params[0] = NULL;

	pgxml_parser_init();


	if (VARDATA(doct)[0] == '<')
		doctree = xmlParseMemory((char *) VARDATA(doct), VARSIZE(doct) - VARHDRSZ);
	else
		doctree = xmlParseFile(GET_STR(doct));

	if (doctree == NULL)
	{
		xmlCleanupParser();
		elog_error(ERROR, "error parsing XML document", 0);

		PG_RETURN_NULL();
	}

	if (VARDATA(ssheet)[0] == '<')
	{
		ssdoc = xmlParseMemory((char *) VARDATA(ssheet),
							   VARSIZE(ssheet) - VARHDRSZ);
		if (ssdoc == NULL)
		{
			xmlFreeDoc(doctree);
			xmlCleanupParser();
			elog_error(ERROR, "error parsing stylesheet as XML document", 0);
			PG_RETURN_NULL();
		}

		stylesheet = xsltParseStylesheetDoc(ssdoc);
	}
	else
		stylesheet = xsltParseStylesheetFile(GET_STR(ssheet));


	if (stylesheet == NULL)
	{
		xmlFreeDoc(doctree);
		xsltCleanupGlobals();
		xmlCleanupParser();
		elog_error(ERROR, "failed to parse stylesheet", 0);
		PG_RETURN_NULL();
	}

	restree = xsltApplyStylesheet(stylesheet, doctree, params);
	resstat = xsltSaveResultToString(&resstr, &reslen, restree, stylesheet);

	xsltFreeStylesheet(stylesheet);
	xmlFreeDoc(restree);
	xmlFreeDoc(doctree);

	xsltCleanupGlobals();
	xmlCleanupParser();

	if (resstat < 0)
		PG_RETURN_NULL();

	tres = palloc(reslen + VARHDRSZ);
	memcpy(VARDATA(tres), resstr, reslen);
	VARATT_SIZEP(tres) = reslen + VARHDRSZ;

	PG_RETURN_TEXT_P(tres);
}


*/

