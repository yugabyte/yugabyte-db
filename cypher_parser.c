#include "postgres.h"

#include "common/keywords.h"
#include "nodes/pg_list.h"

#include "cypher_gram.h"
#include "cypher_keywords.h"
#include "cypher_parser.h"
#include "scan.h"

int cypher_yylex(YYSTYPE *lvalp, YYLTYPE *llocp, ag_scanner_t scanner)
{
    /*
     * This list must match ag_token_type.
     * 0 means end-of-input.
     */
    const int type_map[] = {
        0,
        INTEGER,
        DECIMAL,
        STRING,
        IDENTIFIER,
        PARAMETER,
        NOT_EQ,
        LT_EQ,
        GT_EQ,
        DOT_DOT,
        PLUS_EQ,
        EQ_TILDE
    };

    ag_token token;

    token = ag_scanner_next_token(scanner);

    switch (token.type)
    {
    case AG_TOKEN_NULL:
        break;
    case AG_TOKEN_INTEGER:
        lvalp->integer = token.value.i;
        break;
    case AG_TOKEN_DECIMAL:
    case AG_TOKEN_STRING:
        lvalp->string = pstrdup(token.value.s);
        break;
    case AG_TOKEN_IDENTIFIER:
    {
        const ScanKeyword *keyword;

        keyword = ScanKeywordLookup(token.value.s, cypher_keywords,
                                    num_cypher_keywords);
        if (keyword == NULL)
        {
            lvalp->string = pstrdup(token.value.s);
            break;
        }

        lvalp->keyword = token.value.s;
        *llocp = token.location;
        return keyword->value;
    }
    case AG_TOKEN_PARAMETER:
        lvalp->string = pstrdup(token.value.s);
        break;
    case AG_TOKEN_LT_GT:
    case AG_TOKEN_LT_EQ:
    case AG_TOKEN_GT_EQ:
    case AG_TOKEN_DOT_DOT:
    case AG_TOKEN_PLUS_EQ:
    case AG_TOKEN_EQ_TILDE:
        break;
    case AG_TOKEN_CHAR:
        *llocp = token.location;
        return token.value.c;
    default:
        ereport(ERROR, (errmsg("unexpected ag_token_type: %d", token.type)));
        break;
    }

    *llocp = token.location;
    return type_map[token.type];
}

void cypher_yyerror(YYLTYPE *llocp, ag_scanner_t scanner,
                    cypher_yy_extra *extra, const char *msg)
{
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    ag_scanner_errmsg(msg, scanner),
                    ag_scanner_errposition(*llocp, scanner)));
}

List *parse_cypher(const char *s)
{
    ag_scanner_t scanner;
    cypher_yy_extra extra;
    int yyresult;

    scanner = ag_scanner_create(s);
    extra.result = NIL;

    yyresult = cypher_yyparse(scanner, &extra);

    ag_scanner_destroy(scanner);

    /*
     * cypher_yyparse() returns 0 if parsing was successful.
     * Otherwise, it returns 1 (invalid input) or 2 (memory exhaustion).
     */
    if (yyresult)
        return NIL;

    return extra.result;
}
