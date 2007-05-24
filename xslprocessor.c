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




/*
 * Returns a new processor instance. This function must be called before the default behavior 
 * of Processor can be changed and if other processor methods need to be used.
 * Syntax
 *
 * FUNCTION newProcessor RETURN Processor;
 */
Datum
orafce_xsl_newProcessor(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
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