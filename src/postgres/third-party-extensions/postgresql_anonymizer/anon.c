/*
 * PostgreSQL Anonymizer
 *
 */

#include "postgres.h"

#if PG_VERSION_NUM >= 120000
#include "access/relation.h"
#include "access/table.h"
#else
#include "access/heapam.h"
#include "access/htup_details.h"
#endif

#include "access/xact.h"
#include "commands/seclabel.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "fmgr.h"
#include "catalog/pg_attrdef.h"
#if PG_VERSION_NUM >= 110000
#include "catalog/pg_attrdef_d.h"
#endif
#include "catalog/pg_authid.h"
#include "catalog/pg_class.h"
#include "catalog/pg_database.h"
#include "catalog/pg_namespace.h"
#include "catalog/namespace.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/planner.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/ruleutils.h"
#include "utils/varlena.h"

PG_MODULE_MAGIC;

/* Definition */
#define PA_MAX_SIZE_MASKING_RULE  1024

/* Saved hook values in case of unload */
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static ProcessUtility_hook_type prev_ProcessUtility_hook = NULL;

/*
 * External Functions
 */
PGDLLEXPORT Datum   anon_get_function_schema(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum   anon_init(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum   anon_masking_expressions_for_table(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum   anon_masking_value_for_column(PG_FUNCTION_ARGS);
PGDLLEXPORT void    _PG_init(void);
PGDLLEXPORT void    _PG_fini(void);

PG_FUNCTION_INFO_V1(anon_get_function_schema);
PG_FUNCTION_INFO_V1(anon_init);
PG_FUNCTION_INFO_V1(anon_masking_expressions_for_table);
PG_FUNCTION_INFO_V1(anon_masking_value_for_column);

/*
 * GUC Parameters
 */

static char * guc_anon_k_anonymity_provider;
static char * guc_anon_masking_policies;
static bool   guc_anon_privacy_by_default;
static bool   guc_anon_restrict_to_trusted_schemas;
static bool   guc_anon_strict_mode;

// Some GUC vars below are not used in the C code
// but they are used in the plpgsql code
// compile with `-Wno-unused-variable` to avoid warnings
static char *guc_anon_algorithm;
static char *guc_anon_mask_schema;
static char *guc_anon_salt;
static char *guc_anon_source_schema;
static bool guc_anon_transparent_dynamic_masking;

/*
 * Internal Functions
 */

static bool   pa_check_function(char * expr);
static bool   pa_check_masking_policies(char **newval, void **extra, GucSource source);
static bool   pa_check_tablesample(const char * tbs);
static bool   pa_check_value(char * expr);
static char * pa_get_masking_policy_for_role(Oid roleid);
static void   pa_masking_policy_object_relabel(const ObjectAddress *object, const char *seclabel);
static bool   pa_has_mask_in_policy(Oid roleid, char *policy);
static bool   pa_has_untrusted_schema(Node *node, void *context);
static bool   pa_is_trusted_namespace(Oid namespaceId, char * policy);
static void   pa_rewrite(Query * query, char * policy);
static void   pa_rewrite_utility(PlannedStmt *pstmt, char * policy);
static char * pa_cast_as_regtype(char * value, int atttypid);
static char * pa_masking_value_for_att(Relation rel, FormData_pg_attribute * att, char * policy);
static char * pa_masking_expressions_for_table(Oid relid, char * policy);
Node *        pa_masking_stmt_for_table(Oid relid, char * policy);
Node *        pa_parse_expression(char * expr);


/*
 * Hook functions
 */

// https://github.com/taminomara/psql-hooks/blob/master/Detailed.md#post_parse_analyze_hook
#if PG_VERSION_NUM >= 140000
static void   pa_post_parse_analyze_hook(ParseState *pstate, Query *query, JumbleState *jstate);
#else
static void   pa_post_parse_analyze_hook(ParseState *pstate, Query *query);
#endif

// https://github.com/taminomara/psql-hooks/blob/master/Detailed.md#ProcessUtility_hook
#if PG_VERSION_NUM >= 140000
static void
pa_ProcessUtility_hook( PlannedStmt *pstmt,
                        const char *queryString,
                        bool readOnlyTree,
                        ProcessUtilityContext context,
                        ParamListInfo params,
                        QueryEnvironment *queryEnv,
                        DestReceiver *dest,
                        QueryCompletion *qc);
#elif PG_VERSION_NUM >= 130000
static void
pa_ProcessUtility_hook( PlannedStmt *pstmt,
                        const char *queryString,
                        ProcessUtilityContext context,
                        ParamListInfo params,
                        QueryEnvironment *queryEnv,
                        DestReceiver *dest,
                        QueryCompletion *qc);
#else
static void
pa_ProcessUtility_hook( PlannedStmt *pstmt,
                        const char *queryString,
                        ProcessUtilityContext context,
                        ParamListInfo params,
                        QueryEnvironment *queryEnv,
                        DestReceiver *dest,
                        char *completionTag);
#endif

/*
 * Checking the syntax of the masking rules
 */
static void
pa_masking_policy_object_relabel(const ObjectAddress *object, const char *seclabel)
{

  /* SECURITY LABEL FOR anon ON COLUMN foo.bar IS NULL */
  if (seclabel == NULL) return;

  switch (object->classId)
  {
    /* SECURITY LABEL FOR anon ON DATABASE d IS 'TABLESAMPLE SYSTEM(10)' */
    case DatabaseRelationId:

      if ( pg_strncasecmp(seclabel, "TABLESAMPLE", 11) == 0
        && pa_check_tablesample(seclabel)
      )
        return;

      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
         errmsg("'%s' is not a valid label for a database", seclabel)));
      break;

    case RelationRelationId:

      /* SECURITY LABEL FOR anon ON TABLE t IS 'TABLESAMPLE SYSTEM(10)' */
      if (object->objectSubId == 0)
      {
        if ( pg_strncasecmp(seclabel, "TABLESAMPLE", 11) == 0
          && pa_check_tablesample(seclabel)
        )
          return;

        ereport(ERROR,
          (errcode(ERRCODE_INVALID_NAME),
           errmsg("'%s' is not a valid label for a table", seclabel)));
      }

      /* If the object has a objectSubId, the seclabel is on a column */

      /* Check that the column does not belong to a view */
      if ( get_rel_relkind(object->objectId) == 'v' )
      {
        ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("Masking a view is not supported")));
      }

      /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'MASKED WITH FUNCTION $x$' */
      if ( pg_strncasecmp(seclabel, "MASKED WITH FUNCTION", 20) == 0 ) {
          char * substr=malloc(strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
          strncpy(substr, seclabel+21, strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
          if (pa_check_function(substr)) return;
          ereport(ERROR,
            (errcode(ERRCODE_INVALID_NAME),
            errmsg("'%s' is not a valid masking function", seclabel)));
          break;
      }

      /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'MASKED WITH VALUE $x$' */
      if ( pg_strncasecmp(seclabel, "MASKED WITH VALUE", 17) == 0) {
          char * substr=malloc(strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
          strncpy(substr, seclabel+18, strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
          if (pa_check_value(substr)) return;
          ereport(ERROR,
            (errcode(ERRCODE_INVALID_NAME),
            errmsg("'%s' is not a valid masking value", seclabel)));
          break;
      }

      /* SECURITY LABEL FOR anon ON COLUMN t.i IS 'NOT MASKED' */
      if ( pg_strncasecmp(seclabel, "NOT MASKED", 10) == 0 ) return;

      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
         errmsg("'%s' is not a valid label for a column", seclabel)));
      break;

    /* SECURITY LABEL FOR anon ON ROLE batman IS 'MASKED' */
    case AuthIdRelationId:
      if (pg_strcasecmp(seclabel,"MASKED") == 0)
        return;

      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
         errmsg("'%s' is not a valid label for a role", seclabel)));
      break;

    /* SECURITY LABEL FOR anon ON SCHEMA public IS 'TRUSTED' */
    case NamespaceRelationId:
      if (!superuser())
        ereport(ERROR,
            (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
             errmsg("only superuser can set an anon label for a schema")));

      if (pg_strcasecmp(seclabel,"TRUSTED") == 0)
        return;

      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
         errmsg("'%s' is not a valid label for a schema", seclabel)));
      break;

    /* everything else is unsupported */
    default:
      ereport(ERROR,
          (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
           errmsg("The anon extension does not support labels on this object")));
      break;
  }

  ereport(ERROR,
      (errcode(ERRCODE_INVALID_NAME),
       errmsg("'%s' is not a valid label", seclabel)));
}

/*
 * Checking the syntax of the k-anonymity declarations
 */
static void
pa_k_anonymity_object_relabel(const ObjectAddress *object, const char *seclabel)
{
  switch (object->classId)
  {

    case RelationRelationId:
      /* SECURITY LABEL FOR k_anonymity ON COLUMN t.i IS 'INDIRECT IDENTIFIER' */
      if ( pg_strncasecmp(seclabel, "QUASI IDENTIFIER",17) == 0
        || pg_strncasecmp(seclabel, "INDIRECT IDENTIFIER",19) == 0
      )
      return;

      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
         errmsg("'%s' is not a valid label for a column", seclabel)));
      break;

    /* everything else is unsupported */
    default:
      ereport(ERROR,
          (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
           errmsg("The k_anonymity provider does not support labels on this object")));
      break;
  }

  ereport(ERROR,
      (errcode(ERRCODE_INVALID_NAME),
       errmsg("'%s' is not a valid label", seclabel)));
}

/*
 * Register the extension and declare its GUC variables
 */
void
_PG_init(void)
{
  List *      masking_policies;
  ListCell *  c;
  char *      dup;

  /* GUC parameters */
  DefineCustomStringVariable
  (
    "anon.algorithm",
    "The hash method used for pseudonymizing functions",
    "",
    &guc_anon_algorithm,
    "sha256",
    PGC_SUSET,
    GUC_SUPERUSER_ONLY,
    NULL,
    NULL,
    NULL
  );

  DefineCustomStringVariable
  (
    "anon.k_anonymity_provider",
    "The security label provider used for k-anonymity",
    "",
    &guc_anon_k_anonymity_provider,
    "k_anonymity",
    PGC_SUSET,
    GUC_SUPERUSER_ONLY,
    NULL,
    NULL,
    NULL
  );

  DefineCustomStringVariable
  (
    "anon.masking_policies",
    "Define multiple masking policies (NOT IMPLEMENTED YET)",
    "",
    &guc_anon_masking_policies,
    "anon",
    PGC_SUSET,
    GUC_LIST_INPUT | GUC_SUPERUSER_ONLY,
    pa_check_masking_policies,
    NULL,
    NULL
  );

  DefineCustomStringVariable
  (
    "anon.maskschema",
    "The schema where the dynamic masking views are stored",
    "",
    &guc_anon_mask_schema,
    "mask",
    PGC_SUSET,
    0,
    NULL,
    NULL,
    NULL
  );

  DefineCustomBoolVariable
  (
    "anon.privacy_by_default",
    "Mask all columns with NULL (or the default value for NOT NULL columns).",
    "",
    &guc_anon_privacy_by_default,
    false,
    PGC_SUSET,
    0,
    NULL,
    NULL,
    NULL
  );

  DefineCustomBoolVariable
  (
    "anon.restrict_to_trusted_schemas",
    "Masking filters must be in a trusted schema",
    "Activate this option to prevent non-superuser from using their own masking filters",
    &guc_anon_restrict_to_trusted_schemas,
    true,
    PGC_SUSET,
    GUC_SUPERUSER_ONLY,
    NULL,
    NULL,
    NULL
  );

  DefineCustomStringVariable
  (
    "anon.salt",
    "The salt value used for the pseudonymizing functions",
    "",
    &guc_anon_salt,
    "",
    PGC_SUSET,
    GUC_SUPERUSER_ONLY,
    NULL,
    NULL,
    NULL
  );

  DefineCustomStringVariable
  (
    "anon.sourceschema",
    "The schema where the table are masked by the dynamic masking engine",
    "",
    &guc_anon_source_schema,
    "public",
    PGC_SUSET,
    0,
    NULL,
    NULL,
    NULL
  );

  DefineCustomBoolVariable
  (
    "anon.strict_mode",
    "A masking rule cannot change a column data type, unless you disable this",
    "Disabling the mode is not recommended",
    &guc_anon_strict_mode,
    true,
    PGC_SUSET,
    0,
    NULL,
    NULL,
    NULL
  );

  DefineCustomBoolVariable
  (
    "anon.transparent_dynamic_masking",
    "New masking engine (EXPERIMENTAL)",
    "",
    &guc_anon_transparent_dynamic_masking,
    false,
    PGC_SUSET,
    0,
    NULL,
    NULL,
    NULL
  );

  /* Register the security label provider for k-anonymity */
  register_label_provider(guc_anon_k_anonymity_provider,
                          pa_k_anonymity_object_relabel
  );

  /* Register a security label provider for each masking policy */
  dup = pstrdup(guc_anon_masking_policies);
  SplitGUCList(dup, ',', &masking_policies);
  foreach(c,masking_policies)
  {
    const char      *pg_catalog = "pg_catalog";
    ObjectAddress   schema;
    Oid             schema_id;
    char            *policy = (char *) lfirst(c);

    register_label_provider(policy,pa_masking_policy_object_relabel);

  }

  /* Install the hooks */
  prev_post_parse_analyze_hook = post_parse_analyze_hook;
  post_parse_analyze_hook = pa_post_parse_analyze_hook;

  prev_ProcessUtility_hook = ProcessUtility_hook;
  ProcessUtility_hook = pa_ProcessUtility_hook;
}

/*
 * Unregister and restore the hooks
 */
void
_PG_fini(void) {
  post_parse_analyze_hook = prev_post_parse_analyze_hook;
  ProcessUtility_hook = prev_ProcessUtility_hook;
}

/*
 * anon_init
 *   Initialize the extension
 *
 */
Datum
anon_init(PG_FUNCTION_ARGS)
{
  List *      masking_policies;
  ListCell *  m;
  char *      dup;

  /*
   * In each masking policy, mark `anon` and `pg_catalog` as TRUSTED
   * For some reasons, this can't be done in _PG_init()
   */
  dup = pstrdup(guc_anon_masking_policies);
  SplitGUCList(dup, ',', &masking_policies);
  foreach(m,masking_policies)
  {
    ObjectAddress   anon_schema;
    ObjectAddress   pg_catalog_schema;
    Oid             schema_id;
    char            *policy = (char *) lfirst(m);
    char            *seclabel = NULL;

    register_label_provider(policy,pa_masking_policy_object_relabel);

    schema_id=get_namespace_oid("anon",false);
    ObjectAddressSet(anon_schema, NamespaceRelationId, schema_id);
    seclabel = GetSecurityLabel(&anon_schema, policy);
    if ( ! seclabel || pg_strcasecmp(seclabel,"TRUSTED") != 0)
      SetSecurityLabel(&anon_schema,policy,"TRUSTED");

    schema_id=get_namespace_oid("pg_catalog",false);
    ObjectAddressSet(pg_catalog_schema, NamespaceRelationId, schema_id);
    seclabel = GetSecurityLabel(&pg_catalog_schema, policy);
    if ( ! seclabel || pg_strcasecmp(seclabel,"TRUSTED") != 0)
      SetSecurityLabel(&pg_catalog_schema,policy,"TRUSTED");
  }

  PG_RETURN_BOOL(true);
}

/*
 * pa_cast_as_regtype
 *   decorates a value with a CAST function
 */
static char *
pa_cast_as_regtype(char * value, int atttypid)
{
  StringInfoData casted_value;
  initStringInfo(&casted_value);
  appendStringInfo( &casted_value,
                    "CAST(%s AS %s)",
                    value,
                    format_type_be(atttypid)
  );
  return casted_value.data;
}

/*
 * anon_get_function_schema
 *   Given a function call, e.g. 'anon.fake_city()', returns the namespace of
 *   the function (if possible)
 *
 * returns the schema name if the function is properly schema-qualified
 * returns an empty string if we can't find the schema name
 *
 * We're calling the parser to split the function call into a "raw parse tree".
 * At this stage, there's no way to know if the schema does really exists. We
 * simply deduce the schema name as it is provided.
 *
 */
Datum
anon_get_function_schema(PG_FUNCTION_ARGS)
{
    bool input_is_null = PG_ARGISNULL(0);
    char* function_call= text_to_cstring(PG_GETARG_TEXT_PP(0));
    char query_string[1024];
    List  *raw_parsetree_list;
    SelectStmt *stmt;
    ResTarget  *restarget;
    FuncCall   *fc;

    if (input_is_null) PG_RETURN_NULL();

    /* build a simple SELECT statement and parse it */
    query_string[0] = '\0';
    strlcat(query_string, "SELECT ", sizeof(query_string));
    strlcat(query_string, function_call, sizeof(query_string));
    #if PG_VERSION_NUM >= 140000
    raw_parsetree_list = raw_parser(query_string,RAW_PARSE_DEFAULT);
    #else
    raw_parsetree_list = raw_parser(query_string);
    #endif

    /* accept only one statement */
    if ( list_length(raw_parsetree_list) != 1 ) {
      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
        errmsg("'%s' is not a valid function call", function_call)));
    }

    /* walk throught the parse tree, down to the FuncCall node (if present) */
    #if PG_VERSION_NUM >= 100000
    stmt = (SelectStmt *) linitial_node(RawStmt, raw_parsetree_list)->stmt;
    #else
    stmt = (SelectStmt *) linitial(raw_parsetree_list);
    #endif

    /* accept only one target in the statement */
    if ( list_length(stmt->targetList) != 1 ) {
      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
        errmsg("'%s' is not a valid function call", function_call)));
    }

    restarget = (ResTarget *) linitial(stmt->targetList);
    if (! IsA(restarget->val, FuncCall))
    {
      ereport(ERROR,
        (errcode(ERRCODE_INVALID_NAME),
        errmsg("'%s' is not a valid function call", function_call)));
    }

    /* if the function name is qualified, extract and return the schema name */
    fc = (FuncCall *) restarget->val;
    if ( list_length(fc->funcname) == 2 )
    {
      PG_RETURN_TEXT_P(cstring_to_text(strVal(linitial(fc->funcname))));
    }

    PG_RETURN_TEXT_P(cstring_to_text(""));
}

/*
 * pa_check_function
 *   validate if an expression is a correct masking function
 *
 */
static bool
pa_check_function(char * expr)
{
    FuncCall * fc = (FuncCall *) pa_parse_expression(expr);

    if (fc == NULL) return false;

    if (! IsA(fc, FuncCall)) return false;

    elog(DEBUG1,"expr is a function");

    /* if the function name is not qualified, we cant check the schema */
    if ( guc_anon_restrict_to_trusted_schemas) {

      if (list_length(fc->funcname) != 2 ) return false;

      elog(DEBUG1,"expr is qualified");

      if (pa_has_untrusted_schema((Node *)fc,NULL)) return false;
    }
    return true;
}

static bool
pa_has_untrusted_schema(Node *node, void *context)
{
  if (node == NULL) return false;

  if (IsA(node, FuncCall))
  {
    FuncCall * fc = (FuncCall *) node;
    Oid namespaceId;

    // the function is not qualified, we cannot trust it
    if ( list_length(fc->funcname) != 2 ) return true;

    namespaceId = get_namespace_oid(strVal(linitial(fc->funcname)),false);

    // Returning true will stop the tree walker right away
    // So the logic is inverted: we stop the search once an unstrusted schema
    // is found.
    if ( ! pa_is_trusted_namespace(namespaceId,"anon") ) return true;
  }

  return raw_expression_tree_walker(node,
                                    pa_has_untrusted_schema,
                                    context);
}


/*
 * pa_schema_is_trusted
 *  checks that a schema is declared as trusted
 *
 */
static bool
pa_is_trusted_namespace(Oid namespaceId, char * policy)
{
  ObjectAddress namespace;
  char * seclabel = NULL;

  if (! OidIsValid(namespaceId)) return false;

  ObjectAddressSet(namespace, NamespaceRelationId, namespaceId);
  seclabel = GetSecurityLabel(&namespace, policy);

  return (seclabel && pg_strncasecmp(seclabel, "TRUSTED",7) == 0);
}

/*
 * pa_check_masking_policies
 *   A validation function (hook) called when `anon.masking_policies` is set.
 *
 * see: https://github.com/postgres/postgres/blob/REL_15_STABLE/src/backend/commands/variable.c#L44
 */
static bool
pa_check_masking_policies(char **newval, void **extra, GucSource source)
{
  char    *rawstring;
  List    *elemlist;

  if (!*newval ||  strnlen(*newval,PA_MAX_SIZE_MASKING_RULE) == 0 )
  {
    GUC_check_errdetail("anon.masking_policies cannot be NULL or empty");
    return false;
  }

  /* Need a modifiable copy of string */
  rawstring = pstrdup(*newval);

  /* Parse string into list of identifiers */
  if (!SplitIdentifierString(rawstring, ',', &elemlist))
  {
    /* syntax error in list */
    GUC_check_errdetail("List syntax is invalid.");
    pfree(rawstring);
    list_free(elemlist);
    return false;
  }

  return true;
}


/*
 * pa_check_tablesample
 *   Validate a tablesample expression
 */
bool
pa_check_tablesample(const char * tbs)
{
    char query_string[PA_MAX_SIZE_MASKING_RULE+1];
    List  *raw_parsetree_list;
    SelectStmt *stmt;

    if ( tbs== NULL || strnlen(tbs,PA_MAX_SIZE_MASKING_RULE) == 0)  {
      return false;
    }

    PG_TRY();
    {
      /* build a simple SELECT statement and parse it */
      query_string[0] = '\0';
      strlcat(query_string, "SELECT 1 FROM foo ", sizeof(query_string));
      strlcat(query_string, tbs, sizeof(query_string));
      #if PG_VERSION_NUM >= 140000
      raw_parsetree_list = raw_parser(query_string,RAW_PARSE_DEFAULT);
      #else
      raw_parsetree_list = raw_parser(query_string);
      #endif
    }
    PG_CATCH ();
    {
      return false;
    }
    PG_END_TRY();

    /* accept only one statement */
    if ( list_length(raw_parsetree_list) != 1 ) return false;

    return true;
}

/*
 * pa_check_value
 *   validate if an expression is a correct masking value
 *
 */
static bool
pa_check_value(char * expr)
{
    Node * val = pa_parse_expression(expr);
    if (val == NULL) return false;
    return IsA(val, ColumnRef) || IsA(val,A_Const);
}

/*
 * pa_has_mask_in_policy
 *  checks that a role is masked in the given policy
 *
 */
static bool
pa_has_mask_in_policy(Oid roleid, char *policy)
{
  ObjectAddress role;
  char * seclabel = NULL;

  ObjectAddressSet(role, AuthIdRelationId, roleid);
  seclabel = GetSecurityLabel(&role, policy);

  return (seclabel && pg_strncasecmp(seclabel, "MASKED",6) == 0);
}

/*
 * pa_get_masking_policy
 *  For a given role, returns the policy in which he/she is masked or the NULL
 *  if the role is not masked.
 *
 * https://github.com/fmbiete/pgdisablelogerror/blob/main/disablelogerror.c
 */
static char *
pa_get_masking_policy(Oid roleid)
{
  ListCell   * r;
  char * policy = NULL;

  policy=pa_get_masking_policy_for_role(roleid);
  if (policy) return policy;

// Look at the parent roles
//
//  is_member_of_role(0,0);
//  foreach(r,roles_is_member_of(roleid,1,InvalidOid, NULL))
//  {
//    policy=pa_get_masking_policy_for_role(lfirst_oid(r));
//    if (policy) return policy;
//  }

  /* No masking policy found */
  return NULL;
}

static char *
pa_get_masking_policy_for_role(Oid roleid)
{
  List *      masking_policies;
  ListCell *  c;
  char *      dup = pstrdup(guc_anon_masking_policies);

  SplitGUCList(dup, ',', &masking_policies);
  foreach(c,masking_policies)
  {
    char  * policy = (char *) lfirst(c);
    if (pa_has_mask_in_policy(roleid,policy))
      return policy;
  }

  return NULL;
}

/*
 * pa_masking_stmt_for_table
 *  prepare a Raw Statement object that will replace the authentic relation
 *
 */
Node *
pa_masking_stmt_for_table(Oid relid, char * policy)
{
  const char *    query_format="SELECT %s FROM %s.%s";
  StringInfoData  query_string;
  List  *         raw_parsetree_list;
  Query *         masking_query;

  initStringInfo(&query_string);
  appendStringInfo( &query_string,
                    query_format,
                    pa_masking_expressions_for_table(relid,policy),
                    quote_identifier(get_namespace_name(get_rel_namespace(relid))),
                    quote_identifier(get_rel_name(relid))
  );

  ereport(DEBUG1, (errmsg_internal("%s",query_string.data)));

  raw_parsetree_list = pg_parse_query(query_string.data);
  return (Node *) linitial_node(RawStmt, raw_parsetree_list)->stmt;
}

/*
 * pa_parse_expression
 *   use the Postgres parser to check a given masking expression
 *   returns the parsetree if the expression is valid
 *   or NULL if the expression is invalid
 */
Node *
pa_parse_expression(char * expr)
{
    char query_string[1024];
    List  *raw_parsetree_list;
    SelectStmt *stmt;
    ResTarget  *restarget;
    FuncCall   *fc;

    if ( expr == NULL || strlen(expr) == 0)  {
      return NULL;
    }

    PG_TRY();
    {
      /* build a simple SELECT statement and parse it */
      query_string[0] = '\0';
      strlcat(query_string, "SELECT ", sizeof(query_string));
      strlcat(query_string, expr, sizeof(query_string));
      #if PG_VERSION_NUM >= 140000
      raw_parsetree_list = raw_parser(query_string,RAW_PARSE_DEFAULT);
      #else
      raw_parsetree_list = raw_parser(query_string);
      #endif
    }
    PG_CATCH ();
    {
      return NULL;
    }
    PG_END_TRY();

    /* accept only one statement */
    if ( list_length(raw_parsetree_list) != 1 ) return NULL;

    /* walk throught the parse tree, down to the FuncCall node (if present) */
    #if PG_VERSION_NUM >= 100000
    stmt = (SelectStmt *) linitial_node(RawStmt, raw_parsetree_list)->stmt;
    #else
    stmt = (SelectStmt *) linitial(raw_parsetree_list);
    #endif

    /* accept only one target in the statement */
    if ( list_length(stmt->targetList) != 1 ) return NULL;

    restarget = (ResTarget *) linitial(stmt->targetList);
    return (Node *) restarget->val;
}

/*
 * Post-parse-analysis hook: mask query
 * https://github.com/taminomara/psql-hooks/blob/master/Detailed.md#post_parse_analyze_hook
 */
static void
#if PG_VERSION_NUM >= 140000
pa_post_parse_analyze_hook(ParseState *pstate, Query *query, JumbleState *jstate)
#else
pa_post_parse_analyze_hook(ParseState *pstate, Query *query)
#endif
{
  char * policy = NULL;

  if (prev_post_parse_analyze_hook)
    #if PG_VERSION_NUM >= 140000
    prev_post_parse_analyze_hook(pstate, query, jstate);
    #else
    prev_post_parse_analyze_hook(pstate, query);
    #endif

  if (!IsTransactionState()) return;
  if (!guc_anon_transparent_dynamic_masking) return;

  policy = pa_get_masking_policy(GetUserId());
  if (policy)
    pa_rewrite(query,policy);

  return;
}

/*
 * pa_ProcessUtility_hook
 *   rewrites COPY statement for masked roles
 */
#if PG_VERSION_NUM >= 140000
static void
pa_ProcessUtility_hook( PlannedStmt *pstmt,
                        const char *queryString,
                        bool readOnlyTree,
                        ProcessUtilityContext context,
                        ParamListInfo params,
                        QueryEnvironment *queryEnv,
                        DestReceiver *dest,
                        QueryCompletion *qc)
#elif PG_VERSION_NUM >= 130000
static void
pa_ProcessUtility_hook( PlannedStmt *pstmt,
                        const char *queryString,
                        ProcessUtilityContext context,
                        ParamListInfo params,
                        QueryEnvironment *queryEnv,
                        DestReceiver *dest,
                        QueryCompletion *qc)
#else
static void
pa_ProcessUtility_hook( PlannedStmt *pstmt,
                        const char *queryString,
                        ProcessUtilityContext context,
                        ParamListInfo params,
                        QueryEnvironment *queryEnv,
                        DestReceiver *dest,
                        char *completionTag)
#endif
{
  char * policy = NULL;

  if (IsTransactionState()) {

    policy = pa_get_masking_policy(GetUserId());
    if ( guc_anon_transparent_dynamic_masking && policy)
        pa_rewrite_utility(pstmt,policy);
  }

#if PG_VERSION_NUM >= 140000
  if (prev_ProcessUtility_hook)
      prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
    else
      standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
#elif PG_VERSION_NUM >= 130000
  if (prev_ProcessUtility_hook)
      prev_ProcessUtility_hook(pstmt, queryString, context, params, queryEnv, dest, qc);
    else
      standard_ProcessUtility(pstmt, queryString, context, params, queryEnv, dest, qc);
#else
  if (prev_ProcessUtility_hook)
      prev_ProcessUtility_hook(pstmt, queryString, context, params, queryEnv, dest, completionTag);
    else
      standard_ProcessUtility(pstmt, queryString, context, params, queryEnv, dest, completionTag);
#endif

}

/*
 * pa_rewrite
 */
static void
pa_rewrite(Query * query, char * policy)
{
/*
      ereport(ERROR,
        (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
        errmsg("NOT IMPLEMENTED YET")));
*/
}

/*
 * pa_rewrite_utility
 *   In a COPY statement, substitute a masked relation by its
 *   masking view
 */
static void
pa_rewrite_utility(PlannedStmt *pstmt, char * policy)
{
  Node *    parsetree = pstmt->utilityStmt;
  int       readonly_flags;

  Assert(IsA(pstmt, PlannedStmt));
  Assert(pstmt->commandType == CMD_UTILITY);

  if ( IsA(parsetree, ExplainStmt)
    || IsA(parsetree, TruncateStmt)
  )
  {
    ereport(ERROR,
            ( errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
              errmsg("role \"%s\" is masked",
                     GetUserNameFromId(GetUserId(), false)),
              errdetail("Masked roles are read-only.")
            ));
  }

  if (IsA(parsetree, CopyStmt))
  {
    /* https://doxygen.postgresql.org/structCopyStmt.html */
    CopyStmt   * copystmt = (CopyStmt *) parsetree;
    ereport(DEBUG1, (errmsg_internal("COPY found")));
    if (! copystmt->is_from)
    {
      Oid relid;

      /* replace the relation by the masking subquery */
      relid=RangeVarGetRelid(copystmt->relation,AccessShareLock,false);
      copystmt->relation = NULL;
      copystmt->attlist = NULL;
      copystmt->query=pa_masking_stmt_for_table(relid,policy);
      ereport(DEBUG1, (errmsg_internal("COPY rewritten")));
    }
  }
}

/*
 * pa_masking_expression_for_att
 *  returns the value for an attribute based on its masking rule (if any),
 * which can be either:
 *     - the attribute name (i.e. the authentic value)
 *     - the function or value from the masking rule
 *     - the defaut value of the column
 *     - "NULL"
 */
static char *
pa_masking_value_for_att(Relation rel, FormData_pg_attribute * att, char * policy)
{
  Oid relid;
  ObjectAddress columnobject;
  char * seclabel = NULL;
  char * attname = (char *)quote_identifier(NameStr(att->attname));

  // Get the masking rule, if any
  relid=RelationGetRelid(rel);
  ObjectAddressSubSet(columnobject, RelationRelationId, relid, att->attnum);
  seclabel = GetSecurityLabel(&columnobject, policy);

  // No masking rule found && Privacy By Default is off,
  // the authentic value is shown
  if (!seclabel && !guc_anon_privacy_by_default) return attname;

  // A masking rule was found
  if (seclabel && pg_strncasecmp(seclabel, "MASKED WITH FUNCTION", 20) == 0)
  {
    char * substr=malloc(strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
    strlcpy(substr, seclabel+21, strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
    if (guc_anon_strict_mode) return pa_cast_as_regtype(substr, att->atttypid);
    return substr;
  }

  if (seclabel && pg_strncasecmp(seclabel, "MASKED WITH VALUE", 17) == 0)
  {
    char * substr=malloc(strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
    strlcpy(substr, seclabel+18, strnlen(seclabel,PA_MAX_SIZE_MASKING_RULE));
    if (guc_anon_strict_mode) return pa_cast_as_regtype(substr, att->atttypid);
    return substr;
  }

  // The column is declared as not masked, the authentic value is show
  if (seclabel && pg_strncasecmp(seclabel,"NOT MASKED", 10) == 0) return attname;

  // At this stage, we know privacy_by_default is on
  // Let's try to find the default value of the column
  if (att->atthasdef)
  {
    int i;
    TupleDesc reldesc;

    reldesc = RelationGetDescr(rel);
    for(i=0; i< reldesc->constr->num_defval; i++)
    {
      if (reldesc->constr->defval[i].adnum == att->attnum )
        return deparse_expression(stringToNode(reldesc->constr->defval[i].adbin),
                                  NIL, false, false);
    }

  }

  // No default value, NULL is the last possibility
  return "NULL";
}

/*
 * pa_masking_expressions_for_table
 *   returns the "select clause filters" that will mask the authentic data
 *   of a table for a given masking policy
 */
static char *
pa_masking_expressions_for_table(Oid relid, char * policy)
{
  char            comma[] = " ";
  Relation        rel;
  TupleDesc       reldesc;
  StringInfoData  filters;
  int             i;

  rel = relation_open(relid, AccessShareLock);

  initStringInfo(&filters);
  reldesc = RelationGetDescr(rel);

  for (i = 0; i < reldesc->natts; i++)
  {
    FormData_pg_attribute * a;

    a = TupleDescAttr(reldesc, i);
    if (a->attisdropped) continue;

    appendStringInfoString(&filters,comma);
    appendStringInfo( &filters,
                      "%s AS %s",
                      pa_masking_value_for_att(rel,a,policy),
                      (char *)quote_identifier(NameStr(a->attname))
                    );
    comma[0]=',';
  }
  relation_close(rel, NoLock);

  return filters.data;
}



/*
 * anon_masking_expressions_for_table
 *   returns the "select clause filters" that will mask the authentic data
 *   of a table for a given masking policy
 */
Datum
anon_masking_expressions_for_table(PG_FUNCTION_ARGS)
{
  Oid             relid = PG_GETARG_OID(0);
  char *          masking_policy = text_to_cstring(PG_GETARG_TEXT_PP(1));
  char            comma[] = " ";
  Relation        rel;
  TupleDesc       reldesc;
  StringInfoData  filters;
  int             i;

  if (PG_ARGISNULL(0) || PG_ARGISNULL(1)) PG_RETURN_NULL();

  rel = relation_open(relid, AccessShareLock);
  if (!rel) PG_RETURN_NULL();

  initStringInfo(&filters);
  reldesc = RelationGetDescr(rel);

  for (i = 0; i < reldesc->natts; i++)
  {
    FormData_pg_attribute * a;

    a = TupleDescAttr(reldesc, i);
    if (a->attisdropped) continue;

    appendStringInfoString(&filters,comma);
    appendStringInfo( &filters,
                      "%s AS %s",
                      pa_masking_value_for_att(rel,a,masking_policy),
                      (char *)quote_identifier(NameStr(a->attname))
                    );
    comma[0]=',';
  }
  relation_close(rel, NoLock);

  PG_RETURN_TEXT_P(cstring_to_text(filters.data));
}


/*
 * anon_masking_expression_for_column
 *   returns the masking filter that will mask the authentic data
 *   of a column for a given masking policy
 */
Datum
anon_masking_value_for_column(PG_FUNCTION_ARGS)
{
  Oid             relid = PG_GETARG_OID(0);
  int             colnum = PG_GETARG_INT16(1); // numbered from 1 up
  char *          masking_policy = text_to_cstring(PG_GETARG_TEXT_PP(2));
  Relation        rel;
  TupleDesc       reldesc;
  FormData_pg_attribute *a;
  StringInfoData  masking_value;

  if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2)) PG_RETURN_NULL();

  rel = relation_open(relid, AccessShareLock);
  if (!rel) PG_RETURN_NULL();

  reldesc = RelationGetDescr(rel);
  // Here attributes are numbered from 0 up
  a = TupleDescAttr(reldesc, colnum - 1);
  if (a->attisdropped) PG_RETURN_NULL();

  initStringInfo(&masking_value);
  appendStringInfoString( &masking_value,
                    pa_masking_value_for_att(rel,a,masking_policy)
                  );
  relation_close(rel, NoLock);
  PG_RETURN_TEXT_P(cstring_to_text(masking_value.data));
}

//ereport(NOTICE, (errmsg_internal("")));
