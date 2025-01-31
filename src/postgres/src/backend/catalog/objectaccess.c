/* -------------------------------------------------------------------------
 *
 * objectaccess.c
 *		functions for object_access_hook on various events
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/objectaccess.h"
#include "catalog/pg_class.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_proc.h"

/*
 * Hook on object accesses.  This is intended as infrastructure for security
 * and logging plugins.
 */
object_access_hook_type object_access_hook = NULL;
object_access_hook_type_str object_access_hook_str = NULL;


/*
 * RunObjectPostCreateHook
 *
 * OAT_POST_CREATE object ID based event hook entrypoint
 */
void
RunObjectPostCreateHook(Oid classId, Oid objectId, int subId,
						bool is_internal)
{
	ObjectAccessPostCreate pc_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook != NULL);

	memset(&pc_arg, 0, sizeof(ObjectAccessPostCreate));
	pc_arg.is_internal = is_internal;

	(*object_access_hook) (OAT_POST_CREATE,
						   classId, objectId, subId,
						   (void *) &pc_arg);
}

/*
 * RunObjectDropHook
 *
 * OAT_DROP object ID based event hook entrypoint
 */
void
RunObjectDropHook(Oid classId, Oid objectId, int subId,
				  int dropflags)
{
	ObjectAccessDrop drop_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook != NULL);

	memset(&drop_arg, 0, sizeof(ObjectAccessDrop));
	drop_arg.dropflags = dropflags;

	(*object_access_hook) (OAT_DROP,
						   classId, objectId, subId,
						   (void *) &drop_arg);
}

/*
 * RunObjectTruncateHook
 *
 * OAT_TRUNCATE object ID based event hook entrypoint
 */
void
RunObjectTruncateHook(Oid objectId)
{
	/* caller should check, but just in case... */
	Assert(object_access_hook != NULL);

	(*object_access_hook) (OAT_TRUNCATE,
						   RelationRelationId, objectId, 0,
						   NULL);
}

/*
 * RunObjectPostAlterHook
 *
 * OAT_POST_ALTER object ID based event hook entrypoint
 */
void
RunObjectPostAlterHook(Oid classId, Oid objectId, int subId,
					   Oid auxiliaryId, bool is_internal)
{
	ObjectAccessPostAlter pa_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook != NULL);

	memset(&pa_arg, 0, sizeof(ObjectAccessPostAlter));
	pa_arg.auxiliary_id = auxiliaryId;
	pa_arg.is_internal = is_internal;

	(*object_access_hook) (OAT_POST_ALTER,
						   classId, objectId, subId,
						   (void *) &pa_arg);
}

/*
 * RunNamespaceSearchHook
 *
 * OAT_NAMESPACE_SEARCH object ID based event hook entrypoint
 */
bool
RunNamespaceSearchHook(Oid objectId, bool ereport_on_violation)
{
	ObjectAccessNamespaceSearch ns_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook != NULL);

	memset(&ns_arg, 0, sizeof(ObjectAccessNamespaceSearch));
	ns_arg.ereport_on_violation = ereport_on_violation;
	ns_arg.result = true;

	(*object_access_hook) (OAT_NAMESPACE_SEARCH,
						   NamespaceRelationId, objectId, 0,
						   (void *) &ns_arg);

	return ns_arg.result;
}

/*
 * RunFunctionExecuteHook
 *
 * OAT_FUNCTION_EXECUTE object ID based event hook entrypoint
 */
void
RunFunctionExecuteHook(Oid objectId)
{
	/* caller should check, but just in case... */
	Assert(object_access_hook != NULL);

	(*object_access_hook) (OAT_FUNCTION_EXECUTE,
						   ProcedureRelationId, objectId, 0,
						   NULL);
}

/* String versions */


/*
 * RunObjectPostCreateHookStr
 *
 * OAT_POST_CREATE object name based event hook entrypoint
 */
void
RunObjectPostCreateHookStr(Oid classId, const char *objectName, int subId,
						   bool is_internal)
{
	ObjectAccessPostCreate pc_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook_str != NULL);

	memset(&pc_arg, 0, sizeof(ObjectAccessPostCreate));
	pc_arg.is_internal = is_internal;

	(*object_access_hook_str) (OAT_POST_CREATE,
							   classId, objectName, subId,
							   (void *) &pc_arg);
}

/*
 * RunObjectDropHookStr
 *
 * OAT_DROP object name based event hook entrypoint
 */
void
RunObjectDropHookStr(Oid classId, const char *objectName, int subId,
					 int dropflags)
{
	ObjectAccessDrop drop_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook_str != NULL);

	memset(&drop_arg, 0, sizeof(ObjectAccessDrop));
	drop_arg.dropflags = dropflags;

	(*object_access_hook_str) (OAT_DROP,
							   classId, objectName, subId,
							   (void *) &drop_arg);
}

/*
 * RunObjectTruncateHookStr
 *
 * OAT_TRUNCATE object name based event hook entrypoint
 */
void
RunObjectTruncateHookStr(const char *objectName)
{
	/* caller should check, but just in case... */
	Assert(object_access_hook_str != NULL);

	(*object_access_hook_str) (OAT_TRUNCATE,
							   RelationRelationId, objectName, 0,
							   NULL);
}

/*
 * RunObjectPostAlterHookStr
 *
 * OAT_POST_ALTER object name based event hook entrypoint
 */
void
RunObjectPostAlterHookStr(Oid classId, const char *objectName, int subId,
						  Oid auxiliaryId, bool is_internal)
{
	ObjectAccessPostAlter pa_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook_str != NULL);

	memset(&pa_arg, 0, sizeof(ObjectAccessPostAlter));
	pa_arg.auxiliary_id = auxiliaryId;
	pa_arg.is_internal = is_internal;

	(*object_access_hook_str) (OAT_POST_ALTER,
							   classId, objectName, subId,
							   (void *) &pa_arg);
}

/*
 * RunNamespaceSearchHookStr
 *
 * OAT_NAMESPACE_SEARCH object name based event hook entrypoint
 */
bool
RunNamespaceSearchHookStr(const char *objectName, bool ereport_on_violation)
{
	ObjectAccessNamespaceSearch ns_arg;

	/* caller should check, but just in case... */
	Assert(object_access_hook_str != NULL);

	memset(&ns_arg, 0, sizeof(ObjectAccessNamespaceSearch));
	ns_arg.ereport_on_violation = ereport_on_violation;
	ns_arg.result = true;

	(*object_access_hook_str) (OAT_NAMESPACE_SEARCH,
							   NamespaceRelationId, objectName, 0,
							   (void *) &ns_arg);

	return ns_arg.result;
}

/*
 * RunFunctionExecuteHookStr
 *
 * OAT_FUNCTION_EXECUTE object name based event hook entrypoint
 */
void
RunFunctionExecuteHookStr(const char *objectName)
{
	/* caller should check, but just in case... */
	Assert(object_access_hook_str != NULL);

	(*object_access_hook_str) (OAT_FUNCTION_EXECUTE,
							   ProcedureRelationId, objectName, 0,
							   NULL);
}
