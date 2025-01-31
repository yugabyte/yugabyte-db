/*------------------------------------------------------------------------------
 * pgaudit.c
 *
 * An audit logging extension for PostgreSQL. Provides detailed logging classes,
 * object level logging, and fully-qualified object names for all DML and DDL
 * statements where possible (See README.md for details).
 *
 * Copyright (c) 2014-2022, PostgreSQL Global Development Group
 *------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "access/relation.h"
#include "catalog/catalog.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_class.h"
#include "catalog/namespace.h"
#include "commands/dbcommands.h"
#include "commands/extension.h"
#include "catalog/pg_proc.h"
#include "commands/event_trigger.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "libpq/auth.h"
#include "nodes/nodes.h"
#include "nodes/params.h"
#include "tcop/utility.h"
#include "tcop/deparse_utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/varlena.h"

/* YB includes. */
#include "pg_yb_utils.h"

PG_MODULE_MAGIC;

void _PG_init(void);

PG_FUNCTION_INFO_V1(pgaudit_ddl_command_end);
PG_FUNCTION_INFO_V1(pgaudit_sql_drop);

/*
 * Log Classes
 *
 * pgAudit categorizes actions into classes (eg: DDL, FUNCTION calls, READ
 * queries, WRITE queries).  A GUC is provided for the administrator to
 * configure which class (or classes) of actions to include in the
 * audit log.  We track the currently active set of classes using
 * auditLogBitmap.
 */

/* Bits within auditLogBitmap, defines the classes we understand */
#define LOG_DDL         (1 << 0)    /* CREATE/DROP/ALTER objects */
#define LOG_FUNCTION    (1 << 1)    /* Functions and DO blocks */
#define LOG_MISC        (1 << 2)    /* Statements not covered */
#define LOG_READ        (1 << 3)    /* SELECTs */
#define LOG_ROLE        (1 << 4)    /* GRANT/REVOKE, CREATE/ALTER/DROP ROLE */
#define LOG_WRITE       (1 << 5)    /* INSERT, UPDATE, DELETE, TRUNCATE */
#define LOG_MISC_SET    (1 << 6)    /* SET ... */

#define LOG_NONE        0               /* nothing */
#define LOG_ALL         (0xFFFFFFFF)    /* All */

/* GUC variable for pgaudit.log, which defines the classes to log. */
char *auditLog = NULL;

/* Bitmap of classes selected */
static int auditLogBitmap = LOG_NONE;

/*
 * String constants for log classes - used when processing tokens in the
 * pgaudit.log GUC.
 */
#define CLASS_DDL       "DDL"
#define CLASS_FUNCTION  "FUNCTION"
#define CLASS_MISC      "MISC"
#define CLASS_MISC_SET  "MISC_SET"
#define CLASS_READ      "READ"
#define CLASS_ROLE      "ROLE"
#define CLASS_WRITE     "WRITE"

#define CLASS_NONE      "NONE"
#define CLASS_ALL       "ALL"

/*
 * GUC variable for pgaudit.log_catalog
 *
 * Administrators can choose to NOT log queries when all relations used in
 * the query are in pg_catalog.  Interactive sessions (eg: psql) can cause
 * a lot of noise in the logs which might be uninteresting.
 */
bool auditLogCatalog = true;

/*
 * GUC variable for pgaudit.log_client
 *
 * Specifies whether audit messages should be visible to the client.  This
 * setting should generally be left disabled but may be useful for debugging or
 * other purposes.
 */
bool auditLogClient = false;

/*
 * GUC variable for pgaudit.log_level
 *
 * Administrators can choose which log level the audit log is to be logged
 * at.  The default level is LOG, which goes into the server log but does
 * not go to the client.  Set to NOTICE in the regression tests.
 */
char *auditLogLevelString = NULL;
int auditLogLevel = LOG;

/*
 * GUC variable for pgaudit.log_parameter
 *
 * Administrators can choose if parameters passed into a statement are
 * included in the audit log.
 */
bool auditLogParameter = false;

/*
 * GUC variable for pgaudit.log_relation
 *
 * Administrators can choose, in SESSION logging, to log each relation involved
 * in READ/WRITE class queries.  By default, SESSION logs include the query but
 * do not have a log entry for each relation.
 */
bool auditLogRelation = false;

/*
 * GUC variable for pgaudit.log_rows
 *
 * Administrators can choose if the rows retrieved or affected by a statement
 * are included in the audit log.
 */
bool auditLogRows = false;

/*
 * GUC variable for pgaudit.log_statement
 *
 * Administrators can choose to not have the full statement text logged.
 */
bool auditLogStatement = true;

/*
 * GUC variable for pgaudit.log_statement_once
 *
 * Administrators can choose to have the statement run logged only once instead
 * of on every line.  By default, the statement is repeated on every line of
 * the audit log to facilitate searching, but this can cause the log to be
 * unnecessairly bloated in some environments.
 */
bool auditLogStatementOnce = false;

/*
 * GUC variable for pgaudit.role
 *
 * Administrators can choose which role to base OBJECT auditing off of.
 * Object-level auditing uses the privileges which are granted to this role to
 * determine if a statement should be logged.
 */
char *auditRole = NULL;

/*
 * String constants for the audit log fields.
 */

/*
 * Audit type, which is responsbile for the log message
 */
#define AUDIT_TYPE_OBJECT   "OBJECT"
#define AUDIT_TYPE_SESSION  "SESSION"

/*
 * Object type, used for SELECT/DML statements and function calls.
 *
 * For relation objects, this is essentially relkind (though we do not have
 * access to a function which will just return a string given a relkind;
 * getRelationTypeDescription() comes close but is not public currently).
 *
 * We also handle functions, so it isn't quite as simple as just relkind.
 *
 * This should be kept consistent with what is returned from
 * pg_event_trigger_ddl_commands(), as that's what we use for DDL.
 */
#define OBJECT_TYPE_TABLE           "TABLE"
#define OBJECT_TYPE_INDEX           "INDEX"
#define OBJECT_TYPE_SEQUENCE        "SEQUENCE"
#define OBJECT_TYPE_TOASTVALUE      "TOAST TABLE"
#define OBJECT_TYPE_VIEW            "VIEW"
#define OBJECT_TYPE_MATVIEW         "MATERIALIZED VIEW"
#define OBJECT_TYPE_COMPOSITE_TYPE  "COMPOSITE TYPE"
#define OBJECT_TYPE_FOREIGN_TABLE   "FOREIGN TABLE"
#define OBJECT_TYPE_FUNCTION        "FUNCTION"

#define OBJECT_TYPE_UNKNOWN         "UNKNOWN"

/*
 * String constants used for redacting text after the password token in
 * CREATE/ALTER ROLE commands.
 */
#define TOKEN_PASSWORD             "password"
#define TOKEN_REDACTED             "<REDACTED>"

/*
 * An AuditEvent represents an operation that potentially affects a single
 * object.  If a statement affects multiple objects then multiple AuditEvents
 * are created to represent them.
 */
typedef struct
{
    int64 statementId;          /* Simple counter */
    int64 substatementId;       /* Simple counter */

    LogStmtLevel logStmtLevel;  /* From GetCommandLogLevel when possible,
                                   generated when not. */
    NodeTag commandTag;         /* same here */
    int command;                /* same here */
    const char *objectType;     /* From event trigger when possible,
                                   generated when not. */
    char *objectName;           /* Fully qualified object identification */
    const char *commandText;    /* sourceText / queryString */
    ParamListInfo paramList;    /* QueryDesc/ProcessUtility parameters */

    bool granted;               /* Audit role has object permissions? */
    bool logged;                /* Track if we have logged this event, used
                                   post-ProcessUtility to make sure we log */
    bool statementLogged;       /* Track if we have logged the statement */
    int64 rows;                 /* Track rows processed by the statement */
    MemoryContext queryContext; /* Context for query tracking rows */
    Oid auditOid;               /* Role running query tracking rows  */
    List *rangeTabls;           /* Tables in query tracking rows */
} AuditEvent;

/*
 * A simple FIFO queue to keep track of the current stack of audit events.
 */
typedef struct AuditEventStackItem
{
    struct AuditEventStackItem *next;

    AuditEvent auditEvent;

    int64 stackId;

    MemoryContext contextAudit;
    MemoryContextCallback contextCallback;
} AuditEventStackItem;

AuditEventStackItem *auditEventStack = NULL;

/*
 * pgAudit runs queries of its own when using the event trigger system.
 *
 * Track when we are running a query and don't log it.
 */
static bool internalStatement = false;

/*
 * Track running total for statements and substatements and whether or not
 * anything has been logged since the current statement began.
 */
static int64 statementTotal = 0;
static int64 substatementTotal = 0;
static int64 stackTotal = 0;

static bool statementLogged = false;

/*
 * Stack functions
 *
 * Audit events can go down to multiple levels so a stack is maintained to keep
 * track of them.
 */

/*
 * Respond to callbacks registered with MemoryContextRegisterResetCallback().
 * Removes the event(s) off the stack that have become obsolete once the
 * MemoryContext has been freed.  The callback should always be freeing the top
 * of the stack, but the code is tolerant of out-of-order callbacks.
 */
static void
stack_free(void *stackFree)
{
    AuditEventStackItem *nextItem = auditEventStack;

    /* Only process if the stack contains items */
    while (nextItem != NULL)
    {
        /* Check if this item matches the item to be freed */
        if (nextItem == (AuditEventStackItem *) stackFree)
        {
            /* Move top of stack to the item after the freed item */
            auditEventStack = nextItem->next;

            /* If the stack is not empty */
            if (auditEventStack == NULL)
            {
                /*
                 * Reset internal statement to false.  Normally this will be
                 * reset but in case of an error it might be left set.
                 */
                internalStatement = false;

                /*
                 * Reset sub statement total so the next statement will start
                 * from 1.
                 */
                substatementTotal = 0;

                /*
                 * Reset statement logged so that next statement will be
                 * logged.
                 */
                statementLogged = false;
            }

            return;
        }

        nextItem = nextItem->next;
    }
}

/*
 * Push a new audit event onto the stack and create a new memory context to
 * store it.
 */
static AuditEventStackItem *
stack_push()
{
    MemoryContext contextAudit;
    MemoryContext contextOld;
    AuditEventStackItem *stackItem;

    /*
     * Create a new memory context to contain the stack item.  This will be
     * free'd on stack_pop, or by our callback when the parent context is
     * destroyed.
     */
    contextAudit = AllocSetContextCreate(GetCurrentMemoryContext(),
                                         "pgaudit stack context",
                                         ALLOCSET_DEFAULT_SIZES);

    /* Save the old context to switch back to at the end */
    contextOld = MemoryContextSwitchTo(contextAudit);

    /* Create our new stack item in our context */
    stackItem = palloc0(sizeof(AuditEventStackItem));
    stackItem->contextAudit = contextAudit;
    stackItem->stackId = ++stackTotal;

    /*
     * Setup a callback in case an error happens.  stack_free() will truncate
     * the stack at this item.
     */
    stackItem->contextCallback.func = stack_free;
    stackItem->contextCallback.arg = (void *) stackItem;
    MemoryContextRegisterResetCallback(contextAudit,
                                       &stackItem->contextCallback);

    /* Push new item onto the stack */
    if (auditEventStack != NULL)
        stackItem->next = auditEventStack;
    else
        stackItem->next = NULL;

    auditEventStack = stackItem;

    MemoryContextSwitchTo(contextOld);

    return stackItem;
}

/*
 * Pop an audit event from the stack by deleting the memory context that
 * contains it.  The callback to stack_free() does the actual pop.
 */
static void
stack_pop(int64 stackId)
{
    /* Make sure what we want to delete is at the top of the stack */
    if (auditEventStack != NULL && auditEventStack->stackId == stackId)
        MemoryContextDelete(auditEventStack->contextAudit);
    else
        elog(ERROR, "pgaudit stack item " INT64_FORMAT " not found on top - cannot pop",
             stackId);
}

/*
 * Check that an item is on the stack.  If not, an error will be raised since
 * this is a bad state to be in and it might mean audit records are being lost.
 */
static void
stack_valid(int64 stackId)
{
    AuditEventStackItem *nextItem = auditEventStack;

    /* Look through the stack for the stack entry */
    while (nextItem != NULL && nextItem->stackId != stackId)
        nextItem = nextItem->next;

    /* If we didn't find it, something went wrong. */
    if (nextItem == NULL)
        elog(ERROR, "pgaudit stack item " INT64_FORMAT
             " not found - top of stack is " INT64_FORMAT "",
             stackId,
             auditEventStack == NULL ? (int64) -1 : auditEventStack->stackId);
}

/*
 * Find an item on the stack by the specified query memory context.
 */
static AuditEventStackItem *
stack_find_context(MemoryContext findContext)
{
    AuditEventStackItem *nextItem = auditEventStack;

    /* Look through the stack for the stack entry by query memory context */
    while (nextItem != NULL)
    {
        if (nextItem->auditEvent.queryContext == findContext)
            break;

        nextItem = nextItem->next;
    }

    return nextItem;
}

/*
 * Appends a properly quoted CSV field to StringInfo.
 */
static void
append_valid_csv(StringInfoData *buffer, const char *appendStr)
{
    const char *pChar;

    /*
     * If the append string is null then do nothing.  NULL fields are not
     * quoted in CSV.
     */
    if (appendStr == NULL)
        return;

    /* Only format for CSV if appendStr contains: ", comma, \n, \r */
    if (strstr(appendStr, ",") || strstr(appendStr, "\"") ||
        strstr(appendStr, "\n") || strstr(appendStr, "\r"))
    {
        appendStringInfoCharMacro(buffer, '"');

        for (pChar = appendStr; *pChar; pChar++)
        {
            if (*pChar == '"')    /* double single quotes */
                appendStringInfoCharMacro(buffer, *pChar);

            appendStringInfoCharMacro(buffer, *pChar);
        }

        appendStringInfoCharMacro(buffer, '"');
    }
    /* Else just append */
    else
        appendStringInfoString(buffer, appendStr);
}

/*
 * Takes an AuditEvent, classifies it, then logs it if appropriate.
 *
 * Logging is decided based on if the statement is in one of the classes being
 * logged or if an object used has been marked for auditing.
 *
 * Objects are marked for auditing by the auditor role being granted access
 * to the object.  The kind of access (INSERT, UPDATE, etc) is also considered
 * and logging is only performed when the kind of access matches the granted
 * right on the object.
 *
 * This will need to be updated if new kinds of GRANTs are added.
 */
static void
log_audit_event(AuditEventStackItem *stackItem)
{
    /* By default, put everything in the MISC class. */
    int class = LOG_MISC;
    const char *className = CLASS_MISC;
    MemoryContext contextOld;
    StringInfoData auditStr;

    /*
     * Skip logging script statements if an extension is currently being created
     * or altered. PostgreSQL reports the statement text for each statement in
     * the script as the entire script text, which can blow up the logs. The
     * create/alter statement will still be logged.
     *
     * Since a superuser is responsible for determining which extensions are
     * available, and in most cases installing them, it should not be necessary
     * to log each statement in the script.
     */
    if (creating_extension)
        return;

    /* If this event has already been logged don't log it again */
    if (stackItem->auditEvent.logged)
        return;

    /* Classify the statement using log stmt level and the command tag */
    switch (stackItem->auditEvent.logStmtLevel)
    {
        /* All mods go in WRITE class, except EXECUTE */
        case LOGSTMT_MOD:
            className = CLASS_WRITE;
            class = LOG_WRITE;

            switch (stackItem->auditEvent.commandTag)
            {
                /* Currently, only EXECUTE is different */
                case T_ExecuteStmt:
                    className = CLASS_MISC;
                    class = LOG_MISC;
                    break;
                default:
                    break;
            }
            break;

        /* These are DDL, unless they are ROLE */
        case LOGSTMT_DDL:
            className = CLASS_DDL;
            class = LOG_DDL;

            /* Identify role statements */
            switch (stackItem->auditEvent.commandTag)
            {
                /* In the case of create and alter role redact all text in the
                 * command after the password token for security.  This doesn't
                 * cover all possible cases where passwords can be leaked but
                 * should take care of the most common usage.
                 */
                case T_CreateRoleStmt:
                case T_AlterRoleStmt:

                    if (stackItem->auditEvent.commandText != NULL)
                    {
                        CommandTag command_tag = YbParseCommandTag(stackItem->auditEvent.commandText);
                        stackItem->auditEvent.commandText = YbRedactPasswordIfExists(stackItem->auditEvent.commandText,
                            command_tag);
                    }
                    switch_fallthrough();

                /* Fall through */

                /* Classify role statements */
                case T_GrantStmt:
                case T_GrantRoleStmt:
                case T_DropRoleStmt:
                case T_AlterRoleSetStmt:
                case T_AlterDefaultPrivilegesStmt:
                    className = CLASS_ROLE;
                    class = LOG_ROLE;
                    break;

                /*
                 * Rename and Drop are general and therefore we have to do
                 * an additional check against the command string to see
                 * if they are role or regular DDL.
                 */
                case T_RenameStmt:
                case T_DropStmt:
                    if (stackItem->auditEvent.command == CMDTAG_ALTER_ROLE ||
                        stackItem->auditEvent.command == CMDTAG_DROP_ROLE)
                    {
                        className = CLASS_ROLE;
                        class = LOG_ROLE;
                    }
                    break;

                default:
                    break;
            }
            break;

        /* Classify the rest */
        case LOGSTMT_ALL:
            switch (stackItem->auditEvent.commandTag)
            {
                /* READ statements */
                case T_CopyStmt:
                case T_DeclareCursorStmt:
                case T_SelectStmt:
                case T_PrepareStmt:
                case T_PlannedStmt:
                    className = CLASS_READ;
                    class = LOG_READ;
                    break;

                /* FUNCTION statements */
                case T_DoStmt:
                    className = CLASS_FUNCTION;
                    class = LOG_FUNCTION;
                    break;

                /*
                 * SET statements reported as MISC but filtered by MISC_SET
                 * flags to maintain existing functionality.
                 */
                case T_VariableSetStmt:
                    className = CLASS_MISC;
                    class = LOG_MISC_SET;
                    break;

                default:
                    break;
            }
            break;

        case LOGSTMT_NONE:
            break;
    }

    /*
     * Only log the statement if:
     *
     * 1. The object was selected for audit logging (granted), or
     * 2. The statement belongs to a class that is being logged
     *
     * If neither of these is true, return.
     */
    if (!stackItem->auditEvent.granted && !(auditLogBitmap & class))
        return;

    /*
     * Use audit memory context in case something is not free'd while
     * appending strings and parameters.
     */
    contextOld = MemoryContextSwitchTo(stackItem->contextAudit);

    /* Set statement and substatement IDs */
    if (stackItem->auditEvent.statementId == 0)
    {
        /* If nothing has been logged yet then create a new statement Id */
        if (!statementLogged)
        {
            statementTotal++;
            statementLogged = true;
        }

        stackItem->auditEvent.statementId = statementTotal;
        stackItem->auditEvent.substatementId = ++substatementTotal;
    }

    /*
     * Create the audit substring
     *
     * The type-of-audit-log and statement/substatement ID are handled below,
     * this string is everything else.
     */
    initStringInfo(&auditStr);
    append_valid_csv(&auditStr, GetCommandTagName(stackItem->auditEvent.command));

    appendStringInfoCharMacro(&auditStr, ',');
    append_valid_csv(&auditStr, stackItem->auditEvent.objectType);

    appendStringInfoCharMacro(&auditStr, ',');
    append_valid_csv(&auditStr, stackItem->auditEvent.objectName);

    /*
     * If auditLogStatmentOnce is true, then only log the statement and
     * parameters if they have not already been logged for this substatement.
     */
    appendStringInfoCharMacro(&auditStr, ',');
    if (auditLogStatement && !(stackItem->auditEvent.statementLogged && auditLogStatementOnce))
    {
        append_valid_csv(&auditStr, stackItem->auditEvent.commandText);

        appendStringInfoCharMacro(&auditStr, ',');

        /* Handle parameter logging, if enabled. */
        if (auditLogParameter)
        {
            int paramIdx;
            int numParams;
            StringInfoData paramStrResult;
            ParamListInfo paramList = stackItem->auditEvent.paramList;

            numParams = paramList == NULL ? 0 : paramList->numParams;

            /* Create the param substring */
            initStringInfo(&paramStrResult);

            /* Iterate through all params */
            for (paramIdx = 0; paramList != NULL && paramIdx < numParams;
                 paramIdx++)
            {
                ParamExternData *prm = &paramList->params[paramIdx];
                Oid typeOutput;
                bool typeIsVarLena;
                char *paramStr;

                /* Add a comma for each param */
                if (paramIdx != 0)
                    appendStringInfoCharMacro(&paramStrResult, ',');

                /* Skip if null or if oid is invalid */
                if (prm->isnull || !OidIsValid(prm->ptype))
                    continue;

                /* Output the string */
                getTypeOutputInfo(prm->ptype, &typeOutput, &typeIsVarLena);
                paramStr = OidOutputFunctionCall(typeOutput, prm->value);

                append_valid_csv(&paramStrResult, paramStr);
                pfree(paramStr);
            }

            if (numParams == 0)
                appendStringInfoString(&auditStr, "<none>");
            else
                append_valid_csv(&auditStr, paramStrResult.data);
        }
        else
            appendStringInfoString(&auditStr, "<not logged>");

        stackItem->auditEvent.statementLogged = true;
    }
    /* we were asked to not log it */
    else if (!auditLogStatement)
        appendStringInfoString(&auditStr,
                               "<not logged>,<not logged>");
    else
        appendStringInfoString(&auditStr,
                               "<previously logged>,<previously logged>");

    /* Log rows affected */
    if (auditLogRows)
        appendStringInfo(&auditStr, "," INT64_FORMAT,
                         stackItem->auditEvent.rows);

    /*
     * Log the audit entry.  Note: use of INT64_FORMAT here is bad for
     * translatability, but we currently haven't got translation support in
     * pgaudit anyway.
     */
    ereport(auditLogClient ? auditLogLevel : LOG_SERVER_ONLY,
            (errmsg("AUDIT: %s," INT64_FORMAT "," INT64_FORMAT ",%s,%s",
                    stackItem->auditEvent.granted ?
                    AUDIT_TYPE_OBJECT : AUDIT_TYPE_SESSION,
                    stackItem->auditEvent.statementId,
                    stackItem->auditEvent.substatementId,
                    className,
                    auditStr.data),
                    errhidestmt(true),
                    errhidecontext(true)));

    stackItem->auditEvent.logged = true;

    MemoryContextSwitchTo(contextOld);
}

/*
 * Check if the role or any inherited role has any permission in the mask.  The
 * public role is excluded from this check and superuser permissions are not
 * considered.
 */
static bool
audit_on_acl(Datum aclDatum,
             Oid auditOid,
             AclMode mask)
{
    bool result = false;
    Acl *acl;
    AclItem *aclItemData;
    int aclIndex;
    int aclTotal;

    /* Detoast column's ACL if necessary */
    acl = DatumGetAclP(aclDatum);

    /* Get the acl list and total number of items */
    aclTotal = ACL_NUM(acl);
    aclItemData = ACL_DAT(acl);

    /* Check privileges granted directly to auditOid */
    for (aclIndex = 0; aclIndex < aclTotal; aclIndex++)
    {
        AclItem *aclItem = &aclItemData[aclIndex];

        if (aclItem->ai_grantee == auditOid &&
            aclItem->ai_privs & mask)
        {
            result = true;
            break;
        }
    }

    /*
     * Check privileges granted indirectly via role memberships. We do this in
     * a separate pass to minimize expensive indirect membership tests.  In
     * particular, it's worth testing whether a given ACL entry grants any
     * privileges still of interest before we perform the has_privs_of_role
     * test.
     */
    if (!result)
    {
        for (aclIndex = 0; aclIndex < aclTotal; aclIndex++)
        {
            AclItem *aclItem = &aclItemData[aclIndex];

            /* Don't test public or auditOid (it has been tested already) */
            if (aclItem->ai_grantee == ACL_ID_PUBLIC ||
                aclItem->ai_grantee == auditOid)
                continue;

            /*
             * Check that the role has the required privileges and that it is
             * inherited by auditOid.
             */
            if (aclItem->ai_privs & mask &&
                has_privs_of_role(auditOid, aclItem->ai_grantee))
            {
                result = true;
                break;
            }
        }
    }

    /* If we have a detoasted copy, free it */
    if (acl && (Pointer) acl != DatumGetPointer(aclDatum))
        pfree(acl);

    return result;
}

/*
 * Check if a role has any of the permissions in the mask on a relation.
 */
static bool
audit_on_relation(Oid relOid,
                  Oid auditOid,
                  AclMode mask)
{
    bool result = false;
    HeapTuple tuple;
    Datum aclDatum;
    bool isNull;

    /* Get relation tuple from pg_class */
    tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relOid));
    if (!HeapTupleIsValid(tuple))
        return false;

    /* Get the relation's ACL */
    aclDatum = SysCacheGetAttr(RELOID, tuple, Anum_pg_class_relacl,
                               &isNull);

    /* Only check if non-NULL, since NULL means no permissions */
    if (!isNull)
        result = audit_on_acl(aclDatum, auditOid, mask);

    /* Free the relation tuple */
    ReleaseSysCache(tuple);

    return result;
}

/*
 * Check if a role has any of the permissions in the mask on a column.
 */
static bool
audit_on_attribute(Oid relOid,
                   AttrNumber attNum,
                   Oid auditOid,
                   AclMode mask)
{
    bool result = false;
    HeapTuple attTuple;
    Datum aclDatum;
    bool isNull;

    /* Get the attribute's ACL */
    attTuple = SearchSysCache2(ATTNUM,
                               ObjectIdGetDatum(relOid),
                               Int16GetDatum(attNum));
    if (!HeapTupleIsValid(attTuple))
        return false;

    /* Only consider attributes that have not been dropped */
    if (!((Form_pg_attribute) GETSTRUCT(attTuple))->attisdropped)
    {
        aclDatum = SysCacheGetAttr(ATTNUM, attTuple, Anum_pg_attribute_attacl,
                                   &isNull);

        if (!isNull)
            result = audit_on_acl(aclDatum, auditOid, mask);
    }

    /* Free attribute */
    ReleaseSysCache(attTuple);

    return result;
}

/*
 * Check if a role has any of the permissions in the mask on a column in
 * the provided set.  If the set is empty, then all valid columns in the
 * relation will be tested.
 */
static bool
audit_on_any_attribute(Oid relOid,
                       Oid auditOid,
                       Bitmapset *attributeSet,
                       AclMode mode)
{
    bool result = false;
    AttrNumber col;
    Bitmapset *tmpSet;

    /* If bms is empty then check for any column match */
    if (bms_is_empty(attributeSet))
    {
        HeapTuple classTuple;
        AttrNumber nattrs;
        AttrNumber curr_att;

        /* Get relation to determine total columns */
        classTuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relOid));

        if (!HeapTupleIsValid(classTuple))
            return false;

        nattrs = ((Form_pg_class) GETSTRUCT(classTuple))->relnatts;
        ReleaseSysCache(classTuple);

        /* Check each column */
        for (curr_att = 1; curr_att <= nattrs; curr_att++)
            if (audit_on_attribute(relOid, curr_att, auditOid, mode))
                return true;
    }

    /* bms_first_member is destructive, so make a copy before using it. */
    tmpSet = bms_copy(attributeSet);

    /* Check each column */
    while ((col = bms_first_member(tmpSet)) >= 0)
    {
        if (IsYBRelationById(relOid))
            col += YBFirstLowInvalidAttributeNumber;
        else
            col += FirstLowInvalidHeapAttributeNumber;

        if (col != InvalidAttrNumber &&
            audit_on_attribute(relOid, col, auditOid, mode))
        {
            result = true;
            break;
        }
    }

    bms_free(tmpSet);

    return result;
}

/*
 * Create AuditEvents for SELECT/DML operations via executor permissions checks.
 */
static void
log_select_dml(Oid auditOid, List *rangeTabls)
{
    ListCell *lr;
    bool first = true;
    bool found = false;

    /* Do not log if this is an internal statement */
    if (internalStatement)
        return;

    foreach(lr, rangeTabls)
    {
        Oid relOid;
        Oid relNamespaceOid;
        RangeTblEntry *rte = lfirst(lr);

        /* We only care about tables, and can ignore subqueries etc. */
        if (rte->rtekind != RTE_RELATION)
            continue;

        found = true;

        /*
         * Don't log if the session user is not a member of the current
         * role.  This prevents contents of security definer functions
         * from being logged and supresses foreign key queries unless the
         * session user is the owner of the referenced table.
         */
        if (!is_member_of_role(GetSessionUserId(), GetUserId()))
            return;

        /*
         * If we are not logging all-catalog queries (auditLogCatalog is
         * false) then filter out any system relations here.
         */
        relOid = rte->relid;
        relNamespaceOid = get_rel_namespace(relOid);

        if (!auditLogCatalog && IsCatalogNamespace(relNamespaceOid))
            continue;

        /*
         * Default is that this was not through a grant, to support session
         * logging.  Will be updated below if a grant is found.
         */
        auditEventStack->auditEvent.granted = false;

        /*
         * If this is the first RTE then session log unless auditLogRelation
         * is set.
         */
        if (first && !auditLogRelation)
        {
            log_audit_event(auditEventStack);

            first = false;
        }

        /*
         * We don't have access to the parsetree here, so we have to generate
         * the node type, object type, and command tag by decoding
         * rte->requiredPerms and rte->relkind. For updates we also check
         * rellockmode so that only true UPDATE commands (not
         * SELECT FOR UPDATE, etc.) are logged as UPDATE.
         */
        if (rte->requiredPerms & ACL_INSERT)
        {
            auditEventStack->auditEvent.logStmtLevel = LOGSTMT_MOD;
            auditEventStack->auditEvent.commandTag = T_InsertStmt;
            auditEventStack->auditEvent.command = CMDTAG_INSERT;
        }
        else if (rte->requiredPerms & ACL_UPDATE &&
                 rte->rellockmode >= RowExclusiveLock)
        {
            auditEventStack->auditEvent.logStmtLevel = LOGSTMT_MOD;
            auditEventStack->auditEvent.commandTag = T_UpdateStmt;
            auditEventStack->auditEvent.command = CMDTAG_UPDATE;
        }
        else if (rte->requiredPerms & ACL_DELETE)
        {
            auditEventStack->auditEvent.logStmtLevel = LOGSTMT_MOD;
            auditEventStack->auditEvent.commandTag = T_DeleteStmt;
            auditEventStack->auditEvent.command = CMDTAG_DELETE;
        }
        else if (rte->requiredPerms & ACL_SELECT)
        {
            auditEventStack->auditEvent.logStmtLevel = LOGSTMT_ALL;
            auditEventStack->auditEvent.commandTag = T_SelectStmt;
            auditEventStack->auditEvent.command = CMDTAG_SELECT;
        }
        else
        {
            auditEventStack->auditEvent.logStmtLevel = LOGSTMT_ALL;
            auditEventStack->auditEvent.commandTag = T_Invalid;
            auditEventStack->auditEvent.command = CMDTAG_UNKNOWN;
        }

        /* Use the relation type to assign object type */
        switch (rte->relkind)
        {
            case RELKIND_RELATION:
            case RELKIND_PARTITIONED_TABLE:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_TABLE;
                break;

            case RELKIND_INDEX:
            case RELKIND_PARTITIONED_INDEX:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_INDEX;
                break;

            case RELKIND_SEQUENCE:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_SEQUENCE;
                break;

            case RELKIND_TOASTVALUE:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_TOASTVALUE;
                break;

            case RELKIND_VIEW:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_VIEW;
                break;

            case RELKIND_COMPOSITE_TYPE:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_COMPOSITE_TYPE;
                break;

            case RELKIND_FOREIGN_TABLE:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_FOREIGN_TABLE;
                break;

            case RELKIND_MATVIEW:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_MATVIEW;
                break;

            default:
                auditEventStack->auditEvent.objectType = OBJECT_TYPE_UNKNOWN;
                break;
        }

        /* Get a copy of the relation name and assign it to object name */
        auditEventStack->auditEvent.objectName =
            quote_qualified_identifier(
                get_namespace_name(relNamespaceOid), get_rel_name(relOid));

        /* Perform object auditing only if the audit role is valid */
        if (auditOid != InvalidOid)
        {
            AclMode auditPerms =
                (ACL_SELECT | ACL_UPDATE | ACL_INSERT | ACL_DELETE) &
                rte->requiredPerms;

            /*
             * If any of the required permissions for the relation are granted
             * to the audit role then audit the relation
             */
            if (audit_on_relation(relOid, auditOid, auditPerms))
                auditEventStack->auditEvent.granted = true;

            /*
             * Else check if the audit role has column-level permissions for
             * select, insert, or update.
             */
            else if (auditPerms != 0)
            {
                /*
                 * Check the select columns
                 */
                if (auditPerms & ACL_SELECT)
                    auditEventStack->auditEvent.granted =
                        audit_on_any_attribute(relOid, auditOid,
                                               rte->selectedCols,
                                               ACL_SELECT);

                /*
                 * Check the insert columns
                 */
                if (!auditEventStack->auditEvent.granted &&
                    auditPerms & ACL_INSERT)
                    auditEventStack->auditEvent.granted =
                        audit_on_any_attribute(relOid, auditOid,
                                               rte->insertedCols,
                                               auditPerms);

                /*
                 * Check the update columns
                 */
                if (!auditEventStack->auditEvent.granted &&
                    auditPerms & ACL_UPDATE)
                    auditEventStack->auditEvent.granted =
                        audit_on_any_attribute(relOid, auditOid,
                                               rte->updatedCols,
                                               auditPerms);
            }
        }

        /* Do relation level logging if a grant was found */
        if (auditEventStack->auditEvent.granted)
        {
            auditEventStack->auditEvent.logged = false;
            log_audit_event(auditEventStack);
        }

        /* Do relation level logging if auditLogRelation is set */
        if (auditLogRelation)
        {
            auditEventStack->auditEvent.logged = false;
            auditEventStack->auditEvent.granted = false;
            log_audit_event(auditEventStack);
        }

        pfree(auditEventStack->auditEvent.objectName);
    }

    /*
     * If no tables were found that means that RangeTbls was empty or all
     * relations were in the system schema.  In that case still log a session
     * record.
     */
    if (!found)
    {
        auditEventStack->auditEvent.granted = false;
        auditEventStack->auditEvent.logged = false;

        log_audit_event(auditEventStack);
    }
}

/*
 * Create AuditEvents for non-catalog function execution, as detected by
 * log_object_access() below.
 */
static void
log_function_execute(Oid objectId)
{
    HeapTuple proctup;
    Form_pg_proc proc;
    AuditEventStackItem *stackItem;

    /* Get info about the function. */
    proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(objectId));

    if (!proctup)
        elog(ERROR, "cache lookup failed for function %u", objectId);

    proc = (Form_pg_proc) GETSTRUCT(proctup);

    /*
     * Logging execution of all pg_catalog functions would make the log
     * unusably noisy.
     */
    if (IsCatalogNamespace(proc->pronamespace))
    {
        ReleaseSysCache(proctup);
        return;
    }

    /* Push audit event onto the stack */
    stackItem = stack_push();

    /* Generate the fully-qualified function name. */
    stackItem->auditEvent.objectName =
        quote_qualified_identifier(get_namespace_name(proc->pronamespace),
                                   NameStr(proc->proname));
    ReleaseSysCache(proctup);

    /* Log the function call */
    stackItem->auditEvent.logStmtLevel = LOGSTMT_ALL;
    stackItem->auditEvent.commandTag = T_DoStmt;
    stackItem->auditEvent.command = CMDTAG_EXECUTE;
    stackItem->auditEvent.objectType = OBJECT_TYPE_FUNCTION;
    stackItem->auditEvent.commandText = stackItem->next->auditEvent.commandText;

    log_audit_event(stackItem);

    /* Pop audit event from the stack */
    stack_pop(stackItem->stackId);
}

/*
 * Hook functions
 */
static ExecutorCheckPerms_hook_type next_ExecutorCheckPerms_hook = NULL;
static ProcessUtility_hook_type next_ProcessUtility_hook = NULL;
static object_access_hook_type next_object_access_hook = NULL;
static ExecutorStart_hook_type next_ExecutorStart_hook = NULL;
/* The following hook functions are required to get rows */
static ExecutorRun_hook_type next_ExecutorRun_hook = NULL;
static ExecutorEnd_hook_type next_ExecutorEnd_hook = NULL;

static void pgaudit_NextExecutorStart_hook(QueryDesc *queryDesc, int eflags) {
    /* Call the previous hook or standard function */
    if (next_ExecutorStart_hook)
        next_ExecutorStart_hook(queryDesc, eflags);
    else
        standard_ExecutorStart(queryDesc, eflags);
}

bool isAuditLoggingDisabled() {
  return (auditRole == NULL || auditRole[0] == '\0') && !auditLogBitmap;
}

/*
 * Hook ExecutorStart to get the query text and basic command type for queries
 * that do not contain a table and so can't be idenitified accurately in
 * ExecutorCheckPerms.
 */
static void
pgaudit_ExecutorStart_hook(QueryDesc *queryDesc, int eflags)
{
    AuditEventStackItem *stackItem = NULL;
  if (isAuditLoggingDisabled()) {
    pgaudit_NextExecutorStart_hook(queryDesc, eflags);
    return;
  }

    if (!internalStatement)
    {
        /* Push the audit even onto the stack */
        stackItem = stack_push();

        /* Initialize command using queryDesc->operation */
        switch (queryDesc->operation)
        {
            case CMD_SELECT:
                stackItem->auditEvent.logStmtLevel = LOGSTMT_ALL;
                stackItem->auditEvent.commandTag = T_SelectStmt;
                stackItem->auditEvent.command = CMDTAG_SELECT;
                break;

            case CMD_INSERT:
                stackItem->auditEvent.logStmtLevel = LOGSTMT_MOD;
                stackItem->auditEvent.commandTag = T_InsertStmt;
                stackItem->auditEvent.command = CMDTAG_INSERT;
                break;

            case CMD_UPDATE:
                stackItem->auditEvent.logStmtLevel = LOGSTMT_MOD;
                stackItem->auditEvent.commandTag = T_UpdateStmt;
                stackItem->auditEvent.command = CMDTAG_UPDATE;
                break;

            case CMD_DELETE:
                stackItem->auditEvent.logStmtLevel = LOGSTMT_MOD;
                stackItem->auditEvent.commandTag = T_DeleteStmt;
                stackItem->auditEvent.command = CMDTAG_DELETE;
                break;

            default:
                stackItem->auditEvent.logStmtLevel = LOGSTMT_ALL;
                stackItem->auditEvent.commandTag = T_Invalid;
                stackItem->auditEvent.command = CMDTAG_UNKNOWN;
                break;
        }

        /* Initialize the audit event */
        stackItem->auditEvent.commandText = queryDesc->sourceText;
        stackItem->auditEvent.paramList = copyParamList(queryDesc->params);
    }

    /* Call the previous hook or standard function */
    pgaudit_NextExecutorStart_hook(queryDesc, eflags);

    /*
     * Move the stack memory context to the query memory context.  This needs
     * to be done here because the query context does not exist before the
     * call to standard_ExecutorStart() but the stack item is required by
     * pgaudit_ExecutorCheckPerms_hook() which is called during
     * standard_ExecutorStart().
     */
    if (stackItem)
    {
        MemoryContextSetParent(stackItem->contextAudit,
                               queryDesc->estate->es_query_cxt);

        /* Set query context for tracking rows processed */
        if (auditLogRows)
            stackItem->auditEvent.queryContext = queryDesc->estate->es_query_cxt;
    }
}

/*
 * Hook ExecutorCheckPerms to do session and object auditing for DML.
 */
static bool
pgaudit_ExecutorCheckPerms_hook(List *rangeTabls, bool abort)
{
  Oid auditOid = InvalidOid;

    /* Get the audit oid if the role exists */
  if (auditRole != NULL && auditRole[0] != '\0') {
    auditOid = get_role_oid(auditRole, true);
  }

    /* Log DML if the audit role is valid or session logging is enabled */
    if ((auditOid != InvalidOid || auditLogBitmap != 0) &&
        !IsAbortedTransactionBlockState())
    {
        /* If auditLogRows is on, wait for rows processed to be set */
        if (auditLogRows && auditEventStack != NULL)
        {
            /* Check if the top item is SELECT/INSERT for CREATE TABLE AS */
            if (auditEventStack->auditEvent.commandTag == T_SelectStmt &&
                auditEventStack->next != NULL &&
                auditEventStack->next->auditEvent.command == CMDTAG_CREATE_TABLE_AS &&
                auditEventStack->auditEvent.rangeTabls != NULL)
            {
                /*
                 * First, log the INSERT event for CREATE TABLE AS here.
                 * The SELECT event for CREATE TABLE AS will be logged
                 * in pgaudit_ExecutorEnd_hook() later to get rows.
                 */
                log_select_dml(auditOid, rangeTabls);
            }
            else
            {
                /*
                 * Save auditOid and rangeTabls to call log_select_dml()
                 * in pgaudit_ExecutorEnd_hook() later.
                 */
                auditEventStack->auditEvent.auditOid = auditOid;
                auditEventStack->auditEvent.rangeTabls = rangeTabls;
            }
        }
        else
            log_select_dml(auditOid, rangeTabls);
    }

    /* Call the next hook function */
    if (next_ExecutorCheckPerms_hook &&
        !(*next_ExecutorCheckPerms_hook) (rangeTabls, abort))
        return false;

    return true;
}

static void pgaudit_NextProcessUtility_hook(
    PlannedStmt *pstmt,
    const char *queryString,
    bool readOnlyTree,
    ProcessUtilityContext context,
    ParamListInfo params,
    QueryEnvironment *queryEnv,
    DestReceiver *dest,
    QueryCompletion *qc) {
    /* Call the standard process utility chain. */
    if (next_ProcessUtility_hook)
        (*next_ProcessUtility_hook) (pstmt, queryString, readOnlyTree, context,
                                     params, queryEnv, dest, qc);
    else
        standard_ProcessUtility(pstmt, queryString, readOnlyTree, context,
                                params, queryEnv, dest, qc);
}

/*
 * Hook ExecutorRun to get rows processed by the current statement.
 */
static void
pgaudit_ExecutorRun_hook(QueryDesc *queryDesc, ScanDirection direction, uint64 count, bool execute_once)
{
    AuditEventStackItem *stackItem = NULL;

    /* Call the previous hook or standard function */
    if (next_ExecutorRun_hook)
        next_ExecutorRun_hook(queryDesc, direction, count, execute_once);
    else
        standard_ExecutorRun(queryDesc, direction, count, execute_once);

    if (auditLogRows && !internalStatement)
    {
        /* Find an item from the stack by the query memory context */
        stackItem = stack_find_context(queryDesc->estate->es_query_cxt);

        /* Accumulate the number of rows processed */
        if (stackItem != NULL)
            stackItem->auditEvent.rows += queryDesc->estate->es_processed;
    }
}

/*
 * Hook ExecutorEnd to get rows processed by the current statement.
 */
static void
pgaudit_ExecutorEnd_hook(QueryDesc *queryDesc)
{
    AuditEventStackItem *stackItem = NULL;
    AuditEventStackItem *auditEventStackFull = NULL;

    if (auditLogRows && !internalStatement)
    {
        /* Find an item from the stack by the query memory context */
        stackItem = stack_find_context(queryDesc->estate->es_query_cxt);

        if (stackItem != NULL && stackItem->auditEvent.rangeTabls != NULL)
        {
            /* Reset auditEventStack to use in log_select_dml() */
            auditEventStackFull = auditEventStack;
            auditEventStack = stackItem;

            /* Log SELECT/DML audit entry */
            log_select_dml(stackItem->auditEvent.auditOid,
                           stackItem->auditEvent.rangeTabls);

            /* Switch back to the previous auditEventStack */
            auditEventStack = auditEventStackFull;
        }
    }

    /* Call the previous hook or standard function */
    if (next_ExecutorEnd_hook)
        next_ExecutorEnd_hook(queryDesc);
    else
        standard_ExecutorEnd(queryDesc);
}

/*
 * Hook ProcessUtility to do session auditing for DDL and utility commands.
 */
static void
pgaudit_ProcessUtility_hook(PlannedStmt *pstmt,
                            const char *queryString,
                            bool readOnlyTree,
                            ProcessUtilityContext context,
                            ParamListInfo params,
                            QueryEnvironment *queryEnv,
                            DestReceiver *dest,
                            QueryCompletion *qc)
{
    AuditEventStackItem *stackItem = NULL;
    int64 stackId = 0;

  /*
      Early bail out on pgAudit if
      1. auditLogBitmap is not set (i.e. auditing is not enabled)
      2. auditRole is not set (i.e. you do not have permissions to audit)
      3. if you are not trying to set variable pgaudit.log. In that case you might need to
         log the set statement too and hence you cant bail out.
  */
  if (isAuditLoggingDisabled() &&
      (pstmt->utilityStmt->type != T_VariableSetStmt && !strstr(queryString, "pgaudit."))) {
    pgaudit_NextProcessUtility_hook(
        pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
    return;
  }
    /*
     * Don't audit substatements.  All the substatements we care about should
     * be covered by the event triggers.
     */
    if (context <= PROCESS_UTILITY_QUERY && !IsAbortedTransactionBlockState())
    {
        /* Process top level utility statement */
        if (context == PROCESS_UTILITY_TOPLEVEL)
        {
            /*
             * If the stack is not empty then the only allowed entries are open
             * select, show, and explain cursors
             */
            if (auditEventStack != NULL)
            {
                AuditEventStackItem *nextItem = auditEventStack;

                do
                {
                    if (nextItem->auditEvent.commandTag != T_SelectStmt &&
                        nextItem->auditEvent.commandTag != T_VariableShowStmt &&
                        nextItem->auditEvent.commandTag != T_ExplainStmt &&
                        nextItem->auditEvent.commandTag != T_YbBackfillIndexStmt)
                    {
                        // TODO(Sudheer): Remove the following statements suppressing the
                        // 'stack is not empty' error  once we have a proper fix for
                        // correctly restarting write operations in presence of PGAudit.
                        if (nextItem->auditEvent.commandTag != T_InsertStmt &&
                            nextItem->auditEvent.commandTag != T_DeleteStmt &&
                            nextItem->auditEvent.commandTag != T_UpdateStmt &&
                            nextItem->auditEvent.commandTag != T_ExecuteStmt)
                            elog(ERROR, "pgaudit stack is not empty");
                    }

                    nextItem = nextItem->next;
                }
                while (nextItem != NULL);
            }

            stackItem = stack_push();
            stackItem->auditEvent.paramList = copyParamList(params);
        }
        else
            stackItem = stack_push();

        stackId = stackItem->stackId;
        stackItem->auditEvent.logStmtLevel = GetCommandLogLevel(pstmt->utilityStmt);
        stackItem->auditEvent.commandTag = nodeTag(pstmt->utilityStmt);
        stackItem->auditEvent.command = CreateCommandTag(pstmt->utilityStmt);
        stackItem->auditEvent.commandText = queryString;

        /*
         * If this is a DO block log it before calling the next ProcessUtility
         * hook.
         */
        if (auditLogBitmap & LOG_FUNCTION &&
            stackItem->auditEvent.commandTag == T_DoStmt &&
            !IsAbortedTransactionBlockState())
            log_audit_event(stackItem);

        /*
         * If this is a create/alter extension command log it before calling
         * the next ProcessUtility hook. Otherwise, any warnings will be emitted
         * before the create/alter is logged and errors will prevent it from
         * being logged at all.
         */
        if (auditLogBitmap & LOG_DDL &&
            (stackItem->auditEvent.commandTag == T_CreateExtensionStmt ||
                stackItem->auditEvent.commandTag == T_AlterExtensionStmt) &&
            !IsAbortedTransactionBlockState())
            log_audit_event(stackItem);

        /*
         * A close will free the open cursor which will also free the close
         * audit entry. Immediately log the close and set stackItem to NULL so
         * it won't be logged later.
         */
        if (stackItem->auditEvent.commandTag == T_ClosePortalStmt)
        {
            if (auditLogBitmap & LOG_MISC && !IsAbortedTransactionBlockState())
                log_audit_event(stackItem);

            stackItem = NULL;
        }
    }

    /* Call the standard process utility chain. */
    pgaudit_NextProcessUtility_hook(
        pstmt, queryString, readOnlyTree, context, params, queryEnv, dest,
        qc);

    /*
     * Process the audit event if there is one.  Also check that this event
     * was not popped off the stack by a memory context being free'd
     * elsewhere.
     */
    if (stackItem && !IsAbortedTransactionBlockState())
    {
        /*
         * Make sure the item we want to log is still on the stack - if not
         * then something has gone wrong and an error will be raised.
         */
        stack_valid(stackId);

        /*
         * Log the utility command if logging is on, the command has not
         * already been logged by another hook, and the transaction is not
         * aborted.
         */
        if (auditLogBitmap != 0 && !stackItem->auditEvent.logged)
            log_audit_event(stackItem);
    }
}

/*
 * Hook object_access_hook to provide fully-qualified object names for function
 * calls.
 */
static void
pgaudit_object_access_hook(ObjectAccessType access,
                            Oid classId,
                            Oid objectId,
                            int subId,
                            void *arg)
{
    if (auditLogBitmap & LOG_FUNCTION && access == OAT_FUNCTION_EXECUTE &&
        auditEventStack && !IsAbortedTransactionBlockState())
        log_function_execute(objectId);

    if (next_object_access_hook)
        (*next_object_access_hook) (access, classId, objectId, subId, arg);
}

/*
 * Event trigger functions
 */

/*
 * Supply additional data for (non drop) statements that have event trigger
 * support and can be deparsed.
 *
 * Drop statements are handled below through the older sql_drop event trigger.
 */
Datum
pgaudit_ddl_command_end(PG_FUNCTION_ARGS)
{
    EventTriggerData *eventData;
    int result,
        row;
    TupleDesc spiTupDesc;
    const char *query;
    MemoryContext contextQuery;
    MemoryContext contextOld;

    /* Continue only if session DDL logging is enabled */
    if (~auditLogBitmap & LOG_DDL && ~auditLogBitmap & LOG_ROLE)
        PG_RETURN_NULL();

    /* Be sure the module was loaded */
    if (!auditEventStack)
        elog(ERROR, "pgaudit not loaded before call to "
             "pgaudit_ddl_command_end()");

    /* This is an internal statement - do not log it */
    internalStatement = true;

    /* Make sure the fuction was fired as a trigger */
    if (!CALLED_AS_EVENT_TRIGGER(fcinfo))
        elog(ERROR, "not fired by event trigger manager");

    /* Switch memory context for query */
    contextQuery = AllocSetContextCreate(
                            GetCurrentMemoryContext(),
                            "pgaudit_func_ddl_command_end temporary context",
                            ALLOCSET_DEFAULT_SIZES);
    contextOld = MemoryContextSwitchTo(contextQuery);

    /* Get information about triggered events */
    eventData = (EventTriggerData *) fcinfo->context;

    auditEventStack->auditEvent.logStmtLevel =
        GetCommandLogLevel(eventData->parsetree);
    auditEventStack->auditEvent.commandTag =
        nodeTag(eventData->parsetree);
    auditEventStack->auditEvent.command =
        CreateCommandTag(eventData->parsetree);

    /* Return objects affected by the (non drop) DDL statement */
    query = "SELECT pg_catalog.upper(object_type),\n"
            "       object_identity,\n"
            "       pg_catalog.upper(command_tag)\n"
            "  FROM pg_catalog.pg_event_trigger_ddl_commands()";

    /* Attempt to connect */
    result = SPI_connect();
    if (result < 0)
        elog(ERROR, "pgaudit_ddl_command_end: SPI_connect returned %d",
             result);

    /* Execute the query */
    result = SPI_execute(query, true, 0);
    if (result != SPI_OK_SELECT)
        elog(ERROR, "pgaudit_ddl_command_end: SPI_execute returned %d",
             result);

    /* Iterate returned rows */
    spiTupDesc = SPI_tuptable->tupdesc;
    for (row = 0; row < SPI_processed; row++)
    {
        HeapTuple    spiTuple;

        spiTuple = SPI_tuptable->vals[row];

        /* Supply object name and type for audit event */
        auditEventStack->auditEvent.objectType =
            SPI_getvalue(spiTuple, spiTupDesc, 1);
        auditEventStack->auditEvent.objectName =
            SPI_getvalue(spiTuple, spiTupDesc, 2);
        auditEventStack->auditEvent.command =
            GetCommandTagEnum(SPI_getvalue(spiTuple, spiTupDesc, 3));

        auditEventStack->auditEvent.logged = false;

        /*
         * Identify grant/revoke commands - these are the only non-DDL class
         * commands that should be coming through the event triggers.
         */
        if (auditEventStack->auditEvent.command == CMDTAG_GRANT ||
            auditEventStack->auditEvent.command == CMDTAG_REVOKE)
        {
            NodeTag currentCommandTag = auditEventStack->auditEvent.commandTag;

            auditEventStack->auditEvent.commandTag = T_GrantStmt;
            log_audit_event(auditEventStack);

            auditEventStack->auditEvent.commandTag = currentCommandTag;
        }
        else
            log_audit_event(auditEventStack);
    }

    /* Complete the query */
    SPI_finish();

    MemoryContextSwitchTo(contextOld);
    MemoryContextDelete(contextQuery);

    /* No longer in an internal statement */
    internalStatement = false;

    PG_RETURN_NULL();
}

/*
 * Supply additional data for drop statements that have event trigger support.
 */
Datum
pgaudit_sql_drop(PG_FUNCTION_ARGS)
{
    int result,
        row;
    TupleDesc spiTupDesc;
    const char *query;
    MemoryContext contextQuery;
    MemoryContext contextOld;

    if (~auditLogBitmap & LOG_DDL)
        PG_RETURN_NULL();

    /* Be sure the module was loaded */
    if (!auditEventStack)
        elog(ERROR, "pgaudit not loaded before call to "
             "pgaudit_sql_drop()");

    /* This is an internal statement - do not log it */
    internalStatement = true;

    /* Make sure the fuction was fired as a trigger */
    if (!CALLED_AS_EVENT_TRIGGER(fcinfo))
        elog(ERROR, "not fired by event trigger manager");

    /* Switch memory context for the query */
    contextQuery = AllocSetContextCreate(
                            GetCurrentMemoryContext(),
                            "pgaudit_func_ddl_command_end temporary context",
                            ALLOCSET_DEFAULT_SIZES);
    contextOld = MemoryContextSwitchTo(contextQuery);

    /* Return objects affected by the drop statement */
    query = "SELECT pg_catalog.upper(object_type),\n"
            "       object_identity\n"
            "  FROM pg_catalog.pg_event_trigger_dropped_objects()\n"
            " WHERE pg_catalog.lower(object_type) operator(pg_catalog.<>) 'type'\n"
            "   AND schema_name operator(pg_catalog.<>) 'pg_toast'";

    /* Attempt to connect */
    result = SPI_connect();
    if (result < 0)
        elog(ERROR, "pgaudit_ddl_drop: SPI_connect returned %d",
             result);

    /* Execute the query */
    result = SPI_execute(query, true, 0);
    if (result != SPI_OK_SELECT)
        elog(ERROR, "pgaudit_ddl_drop: SPI_execute returned %d",
             result);

    /* Iterate returned rows */
    spiTupDesc = SPI_tuptable->tupdesc;
    for (row = 0; row < SPI_processed; row++)
    {
        HeapTuple    spiTuple;

        spiTuple = SPI_tuptable->vals[row];

        auditEventStack->auditEvent.objectType =
            SPI_getvalue(spiTuple, spiTupDesc, 1);
        auditEventStack->auditEvent.objectName =
            SPI_getvalue(spiTuple, spiTupDesc, 2);

        auditEventStack->auditEvent.logged = false;
        log_audit_event(auditEventStack);
    }

    /* Complete the query */
    SPI_finish();

    MemoryContextSwitchTo(contextOld);
    MemoryContextDelete(contextQuery);

    /* No longer in an internal statement */
    internalStatement = false;

    PG_RETURN_NULL();
}

/*
 * GUC check and assign functions
 */

/*
 * Take a pgaudit.log value such as "read, write, dml", verify that each of the
 * comma-separated tokens corresponds to a LogClass value, and convert them into
 * a bitmap that log_audit_event can check.
 */
static bool
check_pgaudit_log(char **newVal, void **extra, GucSource source)
{
    List *flagRawList;
    char *rawVal;
    ListCell *lt;
    int *flags;

    /* Make sure newval is a comma-separated list of tokens. */
    rawVal = pstrdup(*newVal);
    if (!SplitIdentifierString(rawVal, ',', &flagRawList))
    {
        GUC_check_errdetail("List syntax is invalid");
        list_free(flagRawList);
        pfree(rawVal);
        return false;
    }

    /*
     * Check that we recognise each token, and add it to the bitmap we're
     * building up in a newly-allocated int *f.
     */
    if (!(flags = (int *) malloc(sizeof(int))))
        return false;

    *flags = 0;

    foreach(lt, flagRawList)
    {
        char *token = (char *) lfirst(lt);
        bool subtract = false;
        int class;

        /* If token is preceded by -, then the token is subtractive */
        if (token[0] == '-')
        {
            token++;
            subtract = true;
        }

        /* Test each token */
        if (pg_strcasecmp(token, CLASS_NONE) == 0)
            class = LOG_NONE;
        else if (pg_strcasecmp(token, CLASS_ALL) == 0)
            class = LOG_ALL;
        else if (pg_strcasecmp(token, CLASS_DDL) == 0)
            class = LOG_DDL;
        else if (pg_strcasecmp(token, CLASS_FUNCTION) == 0)
            class = LOG_FUNCTION;
        else if (pg_strcasecmp(token, CLASS_MISC) == 0)
            class = LOG_MISC | LOG_MISC_SET;
        else if (pg_strcasecmp(token, CLASS_MISC_SET) == 0)
            class = LOG_MISC_SET;
        else if (pg_strcasecmp(token, CLASS_READ) == 0)
            class = LOG_READ;
        else if (pg_strcasecmp(token, CLASS_ROLE) == 0)
            class = LOG_ROLE;
        else if (pg_strcasecmp(token, CLASS_WRITE) == 0)
            class = LOG_WRITE;
        else
        {
            free(flags);
            pfree(rawVal);
            list_free(flagRawList);
            return false;
        }

        /* Add or subtract class bits from the log bitmap */
        if (subtract)
            *flags &= ~class;
        else
            *flags |= class;
    }

    pfree(rawVal);
    list_free(flagRawList);

    /* Store the bitmap for assign_pgaudit_log */
    *extra = flags;

    return true;
}

/*
 * Set pgaudit_log from extra (ignoring newVal, which has already been
 * converted to a bitmap above). Note that extra may not be set if the
 * assignment is to be suppressed.
 */
static void
assign_pgaudit_log(const char *newVal, void *extra)
{
    if (extra)
        auditLogBitmap = *(int *) extra;
}

/*
 * Take a pgaudit.log_level value such as "debug" and check that is is valid.
 * Return the enum value so it does not have to be checked again in the assign
 * function.
 */
static bool
check_pgaudit_log_level(char **newVal, void **extra, GucSource source)
{
    int *logLevel;

    /* Allocate memory to store the log level */
    if (!(logLevel = (int *) malloc(sizeof(int))))
        return false;

    /* Find the log level enum */
    if (pg_strcasecmp(*newVal, "debug") == 0)
        *logLevel = DEBUG2;
    else if (pg_strcasecmp(*newVal, "debug5") == 0)
        *logLevel = DEBUG5;
    else if (pg_strcasecmp(*newVal, "debug4") == 0)
        *logLevel = DEBUG4;
    else if (pg_strcasecmp(*newVal, "debug3") == 0)
        *logLevel = DEBUG3;
    else if (pg_strcasecmp(*newVal, "debug2") == 0)
        *logLevel = DEBUG2;
    else if (pg_strcasecmp(*newVal, "debug1") == 0)
        *logLevel = DEBUG1;
    else if (pg_strcasecmp(*newVal, "info") == 0)
        *logLevel = INFO;
    else if (pg_strcasecmp(*newVal, "notice") == 0)
        *logLevel = NOTICE;
    else if (pg_strcasecmp(*newVal, "warning") == 0)
        *logLevel = WARNING;
    else if (pg_strcasecmp(*newVal, "log") == 0)
        *logLevel = LOG;

    /* Error if the log level enum is not found */
    else
    {
        free(logLevel);
        return false;
    }

    /* Return the log level enum */
    *extra = logLevel;

    return true;
}

/*
 * Set pgaudit_log from extra (ignoring newVal, which has already been
 * converted to an enum above). Note that extra may not be set if the
 * assignment is to be suppressed.
 */
static void
assign_pgaudit_log_level(const char *newVal, void *extra)
{
    if (extra)
        auditLogLevel = *(int *) extra;
}

/*
 * Define GUC variables and install hooks upon module load.
 */
void
_PG_init(void)
{
    /* Be sure we do initialization only once */
    static bool inited = false;

    if (inited)
        return;

    /* Must be loaded with shared_preload_libraries */
    if (!process_shared_preload_libraries_in_progress)
        ereport(ERROR, (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                errmsg("pgaudit must be loaded via shared_preload_libraries")));

    /* Define pgaudit.log */
    DefineCustomStringVariable(
        "pgaudit.log",

        "Specifies which classes of statements will be logged by session audit "
        "logging. Multiple classes can be provided using a comma-separated "
        "list and classes can be subtracted by prefacing the class with a "
        "- sign.",

        NULL,
        &auditLog,
        "none",
        PGC_SUSET,
        GUC_LIST_INPUT | GUC_NOT_IN_SAMPLE,
        check_pgaudit_log,
        assign_pgaudit_log,
        NULL);

    /* Define pgaudit.log_catalog */
    DefineCustomBoolVariable(
        "pgaudit.log_catalog",

        "Specifies that session logging should be enabled in the case where "
        "all relations in a statement are in pg_catalog.  Disabling this "
        "setting will reduce noise in the log from tools like psql and PgAdmin "
        "that query the catalog heavily.",

        NULL,
        &auditLogCatalog,
        true,
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /* Define pgaudit.log_client */
    DefineCustomBoolVariable(
        "pgaudit.log_client",

        "Specifies whether audit messages should be visible to the client. "
        "This setting should generally be left disabled but may be useful for "
        "debugging or other purposes.",

        NULL,
        &auditLogClient,
        false,
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /* Define pgaudit.log_level */
    DefineCustomStringVariable(
        "pgaudit.log_level",

        "Specifies the log level that will be used for log entries. This "
        "setting is used for regression testing and may also be useful to end "
        "users for testing or other purposes.  It is not intended to be used "
        "in a production environment as it may leak which statements are being "
        "logged to the user.",

        NULL,
        &auditLogLevelString,
        "log",
        PGC_SUSET,
        GUC_LIST_INPUT | GUC_NOT_IN_SAMPLE,
        check_pgaudit_log_level,
        assign_pgaudit_log_level,
        NULL);

    /* Define pgaudit.log_parameter */
    DefineCustomBoolVariable(
        "pgaudit.log_parameter",

        "Specifies that audit logging should include the parameters that were "
        "passed with the statement. When parameters are present they will be "
        "be included in CSV format after the statement text.",

        NULL,
        &auditLogParameter,
        false,
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /* Define pgaudit.log_relation */
    DefineCustomBoolVariable(
        "pgaudit.log_relation",

        "Specifies whether session audit logging should create a separate log "
        "entry for each relation referenced in a SELECT or DML statement. "
        "This is a useful shortcut for exhaustive logging without using object "
        "audit logging.",

        NULL,
        &auditLogRelation,
        false,
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /* Define pgaudit.log_rows */
    DefineCustomBoolVariable(
        "pgaudit.log_rows",

        "Specifies whether logging will include the rows retrieved or "
        "affected by a statement.",

        NULL,
        &auditLogRows,
        false,
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /* Define pgaudit.log_statement */
    DefineCustomBoolVariable(
        "pgaudit.log_statement",

        "Specifies whether logging will include the statement text and "
        "parameters.  Depending on requirements, the full statement text might "
        "not be required in the audit log.",

        NULL,
        &auditLogStatement,
        true,
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /* Define pgaudit.log_statement_once */
    DefineCustomBoolVariable(
        "pgaudit.log_statement_once",

        "Specifies whether logging will include the statement text and "
        "parameters with the first log entry for a statement/substatement "
        "combination or with every entry.  Disabling this setting will result "
        "in less verbose logging but may make it more difficult to determine "
        "the statement that generated a log entry, though the "
        "statement/substatement pair along with the process id should suffice "
        "to identify the statement text logged with a previous entry.",

        NULL,
        &auditLogStatementOnce,
        false,
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /* Define pgaudit.role */
    DefineCustomStringVariable(
        "pgaudit.role",

        "Specifies the master role to use for object audit logging.  Multiple "
        "audit roles can be defined by granting them to the master role. This "
        "allows multiple groups to be in charge of different aspects of audit "
        "logging.",

        NULL,
        &auditRole,
        "",
        PGC_SUSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    /*
     * Install our hook functions after saving the existing pointers to
     * preserve the chains.
     */
    next_ExecutorStart_hook = ExecutorStart_hook;
    ExecutorStart_hook = pgaudit_ExecutorStart_hook;

    next_ExecutorCheckPerms_hook = ExecutorCheckPerms_hook;
    ExecutorCheckPerms_hook = pgaudit_ExecutorCheckPerms_hook;

    next_ProcessUtility_hook = ProcessUtility_hook;
    ProcessUtility_hook = pgaudit_ProcessUtility_hook;

    next_object_access_hook = object_access_hook;
    object_access_hook = pgaudit_object_access_hook;

    /* The following hook functions are required to get rows */
    next_ExecutorRun_hook = ExecutorRun_hook;
    ExecutorRun_hook = pgaudit_ExecutorRun_hook;

    next_ExecutorEnd_hook = ExecutorEnd_hook;
    ExecutorEnd_hook = pgaudit_ExecutorEnd_hook;

    /* Log that the extension has completed initialization */
#ifndef EXEC_BACKEND
    ereport(LOG, (errmsg("pgaudit extension initialized")));
#else
    ereport(DEBUG1, (errmsg("pgaudit extension initialized")));
#endif /* EXEC_BACKEND */

    inited = true;
}
