/*-------------------------------------------------------------------------
 *
 * acl.h
 *	  Definition of (and support for) access control list data structures.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/acl.h
 *
 * NOTES
 *	  An ACL array is simply an array of AclItems, representing the union
 *	  of the privileges represented by the individual items.  A zero-length
 *	  array represents "no privileges".
 *
 *	  The order of items in the array is important as client utilities (in
 *	  particular, pg_dump, though possibly other clients) expect to be able
 *	  to issue GRANTs in the ordering of the items in the array.  The reason
 *	  this matters is that GRANTs WITH GRANT OPTION must be before any GRANTs
 *	  which depend on it.  This happens naturally in the backend during
 *	  operations as we update ACLs in-place, new items are appended, and
 *	  existing entries are only removed if there's no dependency on them (no
 *	  GRANT can been based on it, or, if there was, those GRANTs are also
 *	  removed).
 *
 *	  For backward-compatibility purposes we have to allow null ACL entries
 *	  in system catalogs.  A null ACL will be treated as meaning "default
 *	  protection" (i.e., whatever acldefault() returns).
 *-------------------------------------------------------------------------
 */
#ifndef ACL_H
#define ACL_H

#include "access/htup.h"
#include "nodes/parsenodes.h"
#include "parser/parse_node.h"
#include "utils/snapshot.h"


/*
 * typedef AclMode is declared in parsenodes.h, also the individual privilege
 * bit meanings are defined there
 */

#define ACL_ID_PUBLIC	0		/* placeholder for id in a PUBLIC acl item */

/*
 * AclItem
 *
 * Note: must be same size on all platforms, because the size is hardcoded
 * in the pg_type.h entry for aclitem.
 */
typedef struct AclItem
{
	Oid			ai_grantee;		/* ID that this item grants privs to */
	Oid			ai_grantor;		/* grantor of privs */
	AclMode		ai_privs;		/* privilege bits */
} AclItem;

/*
 * The upper 16 bits of the ai_privs field of an AclItem are the grant option
 * bits, and the lower 16 bits are the actual privileges.  We use "rights"
 * to mean the combined grant option and privilege bits fields.
 */
#define ACLITEM_GET_PRIVS(item)    ((item).ai_privs & 0xFFFF)
#define ACLITEM_GET_GOPTIONS(item) (((item).ai_privs >> 16) & 0xFFFF)
#define ACLITEM_GET_RIGHTS(item)   ((item).ai_privs)

#define ACL_GRANT_OPTION_FOR(privs) (((AclMode) (privs) & 0xFFFF) << 16)
#define ACL_OPTION_TO_PRIVS(privs)	(((AclMode) (privs) >> 16) & 0xFFFF)

#define ACLITEM_SET_PRIVS(item,privs) \
  ((item).ai_privs = ((item).ai_privs & ~((AclMode) 0xFFFF)) | \
					 ((AclMode) (privs) & 0xFFFF))
#define ACLITEM_SET_GOPTIONS(item,goptions) \
  ((item).ai_privs = ((item).ai_privs & ~(((AclMode) 0xFFFF) << 16)) | \
					 (((AclMode) (goptions) & 0xFFFF) << 16))
#define ACLITEM_SET_RIGHTS(item,rights) \
  ((item).ai_privs = (AclMode) (rights))

#define ACLITEM_SET_PRIVS_GOPTIONS(item,privs,goptions) \
  ((item).ai_privs = ((AclMode) (privs) & 0xFFFF) | \
					 (((AclMode) (goptions) & 0xFFFF) << 16))


#define ACLITEM_ALL_PRIV_BITS		((AclMode) 0xFFFF)
#define ACLITEM_ALL_GOPTION_BITS	((AclMode) 0xFFFF << 16)

/*
 * Definitions for convenient access to Acl (array of AclItem).
 * These are standard PostgreSQL arrays, but are restricted to have one
 * dimension and no nulls.  We also ignore the lower bound when reading,
 * and set it to one when writing.
 *
 * CAUTION: as of PostgreSQL 7.1, these arrays are toastable (just like all
 * other array types).  Therefore, be careful to detoast them with the
 * macros provided, unless you know for certain that a particular array
 * can't have been toasted.
 */


/*
 * Acl			a one-dimensional array of AclItem
 */
typedef struct ArrayType Acl;

#define ACL_NUM(ACL)			(ARR_DIMS(ACL)[0])
#define ACL_DAT(ACL)			((AclItem *) ARR_DATA_PTR(ACL))
#define ACL_N_SIZE(N)			(ARR_OVERHEAD_NONULLS(1) + ((N) * sizeof(AclItem)))
#define ACL_SIZE(ACL)			ARR_SIZE(ACL)

/*
 * fmgr macros for these types
 */
#define DatumGetAclItemP(X)		   ((AclItem *) DatumGetPointer(X))
#define PG_GETARG_ACLITEM_P(n)	   DatumGetAclItemP(PG_GETARG_DATUM(n))
#define PG_RETURN_ACLITEM_P(x)	   PG_RETURN_POINTER(x)

#define DatumGetAclP(X)			   ((Acl *) PG_DETOAST_DATUM(X))
#define DatumGetAclPCopy(X)		   ((Acl *) PG_DETOAST_DATUM_COPY(X))
#define PG_GETARG_ACL_P(n)		   DatumGetAclP(PG_GETARG_DATUM(n))
#define PG_GETARG_ACL_P_COPY(n)    DatumGetAclPCopy(PG_GETARG_DATUM(n))
#define PG_RETURN_ACL_P(x)		   PG_RETURN_POINTER(x)

/*
 * ACL modification opcodes for aclupdate
 */
#define ACL_MODECHG_ADD			1
#define ACL_MODECHG_DEL			2
#define ACL_MODECHG_EQL			3

/*
 * External representations of the privilege bits --- aclitemin/aclitemout
 * represent each possible privilege bit with a distinct 1-character code
 */
#define ACL_INSERT_CHR			'a' /* formerly known as "append" */
#define ACL_SELECT_CHR			'r' /* formerly known as "read" */
#define ACL_UPDATE_CHR			'w' /* formerly known as "write" */
#define ACL_DELETE_CHR			'd'
#define ACL_TRUNCATE_CHR		'D' /* super-delete, as it were */
#define ACL_REFERENCES_CHR		'x'
#define ACL_TRIGGER_CHR			't'
#define ACL_EXECUTE_CHR			'X'
#define ACL_USAGE_CHR			'U'
#define ACL_CREATE_CHR			'C'
#define ACL_CREATE_TEMP_CHR		'T'
#define ACL_CONNECT_CHR			'c'
#define ACL_SET_CHR				's'
#define ACL_ALTER_SYSTEM_CHR	'A'

/* string holding all privilege code chars, in order by bitmask position */
#define ACL_ALL_RIGHTS_STR	"arwdDxtXUCTcsA"

/*
 * Bitmasks defining "all rights" for each supported object type
 */
#define ACL_ALL_RIGHTS_COLUMN		(ACL_INSERT|ACL_SELECT|ACL_UPDATE|ACL_REFERENCES)
#define ACL_ALL_RIGHTS_RELATION		(ACL_INSERT|ACL_SELECT|ACL_UPDATE|ACL_DELETE|ACL_TRUNCATE|ACL_REFERENCES|ACL_TRIGGER)
#define ACL_ALL_RIGHTS_SEQUENCE		(ACL_USAGE|ACL_SELECT|ACL_UPDATE)
#define ACL_ALL_RIGHTS_DATABASE		(ACL_CREATE|ACL_CREATE_TEMP|ACL_CONNECT)
#define ACL_ALL_RIGHTS_FDW			(ACL_USAGE)
#define ACL_ALL_RIGHTS_FOREIGN_SERVER (ACL_USAGE)
#define ACL_ALL_RIGHTS_FUNCTION		(ACL_EXECUTE)
#define ACL_ALL_RIGHTS_LANGUAGE		(ACL_USAGE)
#define ACL_ALL_RIGHTS_LARGEOBJECT	(ACL_SELECT|ACL_UPDATE)
#define ACL_ALL_RIGHTS_PARAMETER_ACL (ACL_SET|ACL_ALTER_SYSTEM)
#define ACL_ALL_RIGHTS_SCHEMA		(ACL_USAGE|ACL_CREATE)
#define ACL_ALL_RIGHTS_TABLEGROUP	(ACL_CREATE)
#define ACL_ALL_RIGHTS_TABLESPACE	(ACL_CREATE)
#define ACL_ALL_RIGHTS_TYPE			(ACL_USAGE)

/* operation codes for pg_*_aclmask */
typedef enum
{
	ACLMASK_ALL,				/* normal case: compute all bits */
	ACLMASK_ANY					/* return when result is known nonzero */
} AclMaskHow;

/* result codes for pg_*_aclcheck */
typedef enum
{
	ACLCHECK_OK = 0,
	ACLCHECK_NO_PRIV,
	ACLCHECK_NOT_OWNER
} AclResult;


/*
 * routines used internally
 */
extern Acl *acldefault(ObjectType objtype, Oid ownerId);
extern Acl *get_user_default_acl(ObjectType objtype, Oid ownerId,
								 Oid nsp_oid);
extern void recordDependencyOnNewAcl(Oid classId, Oid objectId, int32 objsubId,
									 Oid ownerId, Acl *acl);

extern Acl *aclupdate(const Acl *old_acl, const AclItem *mod_aip,
					  int modechg, Oid ownerId, DropBehavior behavior);
extern Acl *aclnewowner(const Acl *old_acl, Oid oldOwnerId, Oid newOwnerId);
extern Acl *make_empty_acl(void);
extern Acl *aclcopy(const Acl *orig_acl);
extern Acl *aclconcat(const Acl *left_acl, const Acl *right_acl);
extern Acl *aclmerge(const Acl *left_acl, const Acl *right_acl, Oid ownerId);
extern void aclitemsort(Acl *acl);
extern bool aclequal(const Acl *left_acl, const Acl *right_acl);

extern AclMode aclmask(const Acl *acl, Oid roleid, Oid ownerId,
					   AclMode mask, AclMaskHow how);
extern int	aclmembers(const Acl *acl, Oid **roleids);

extern bool has_privs_of_role(Oid member, Oid role);
extern bool is_member_of_role(Oid member, Oid role);
extern bool is_member_of_role_nosuper(Oid member, Oid role);
extern bool is_admin_of_role(Oid member, Oid role);
extern void check_is_member_of_role(Oid member, Oid role);
extern Oid	get_role_oid(const char *rolename, bool missing_ok);
extern Oid	get_role_oid_or_public(const char *rolename);
extern Oid	get_rolespec_oid(const RoleSpec *role, bool missing_ok);
extern void check_rolespec_name(const RoleSpec *role, const char *detail_msg);
extern HeapTuple get_rolespec_tuple(const RoleSpec *role);
extern char *get_rolespec_name(const RoleSpec *role);

extern void select_best_grantor(Oid roleId, AclMode privileges,
								const Acl *acl, Oid ownerId,
								Oid *grantorId, AclMode *grantOptions);

extern void initialize_acl(void);

/*
 * prototypes for functions in aclchk.c
 */
extern void ExecuteGrantStmt(GrantStmt *stmt);
extern void ExecAlterDefaultPrivilegesStmt(ParseState *pstate, AlterDefaultPrivilegesStmt *stmt);

extern void RemoveRoleFromObjectACL(Oid roleid, Oid classid, Oid objid);

extern AclMode pg_attribute_aclmask(Oid table_oid, AttrNumber attnum,
									Oid roleid, AclMode mask, AclMaskHow how);
extern AclMode pg_attribute_aclmask_ext(Oid table_oid, AttrNumber attnum,
										Oid roleid, AclMode mask,
										AclMaskHow how, bool *is_missing);
extern AclMode pg_class_aclmask(Oid table_oid, Oid roleid,
								AclMode mask, AclMaskHow how);
extern AclMode pg_class_aclmask_ext(Oid table_oid, Oid roleid,
									AclMode mask, AclMaskHow how,
									bool *is_missing);
extern AclMode pg_database_aclmask(Oid db_oid, Oid roleid,
								   AclMode mask, AclMaskHow how);
extern AclMode pg_parameter_aclmask(const char *name, Oid roleid,
									AclMode mask, AclMaskHow how);
extern AclMode pg_parameter_acl_aclmask(Oid acl_oid, Oid roleid,
										AclMode mask, AclMaskHow how);
extern AclMode pg_proc_aclmask(Oid proc_oid, Oid roleid,
							   AclMode mask, AclMaskHow how);
extern AclMode pg_language_aclmask(Oid lang_oid, Oid roleid,
								   AclMode mask, AclMaskHow how);
extern AclMode pg_largeobject_aclmask_snapshot(Oid lobj_oid, Oid roleid,
											   AclMode mask, AclMaskHow how, Snapshot snapshot);
extern AclMode pg_namespace_aclmask(Oid nsp_oid, Oid roleid,
									AclMode mask, AclMaskHow how);
extern AclMode pg_tablespace_aclmask(Oid spc_oid, Oid roleid,
									 AclMode mask, AclMaskHow how);
extern AclMode pg_foreign_data_wrapper_aclmask(Oid fdw_oid, Oid roleid,
											   AclMode mask, AclMaskHow how);
extern AclMode pg_foreign_server_aclmask(Oid srv_oid, Oid roleid,
										 AclMode mask, AclMaskHow how);
extern AclMode pg_type_aclmask(Oid type_oid, Oid roleid,
							   AclMode mask, AclMaskHow how);

extern AclResult pg_attribute_aclcheck(Oid table_oid, AttrNumber attnum,
									   Oid roleid, AclMode mode);
extern AclResult pg_attribute_aclcheck_ext(Oid table_oid, AttrNumber attnum,
										   Oid roleid, AclMode mode,
										   bool *is_missing);
extern AclResult pg_attribute_aclcheck_all(Oid table_oid, Oid roleid,
										   AclMode mode, AclMaskHow how);
extern AclResult pg_class_aclcheck(Oid table_oid, Oid roleid, AclMode mode);
extern AclResult pg_class_aclcheck_ext(Oid table_oid, Oid roleid,
									   AclMode mode, bool *is_missing);
extern AclResult pg_database_aclcheck(Oid db_oid, Oid roleid, AclMode mode);
extern AclResult pg_parameter_aclcheck(const char *name, Oid roleid,
									   AclMode mode);
extern AclResult pg_parameter_acl_aclcheck(Oid acl_oid, Oid roleid,
										   AclMode mode);
extern AclResult pg_proc_aclcheck(Oid proc_oid, Oid roleid, AclMode mode);
extern AclResult pg_language_aclcheck(Oid lang_oid, Oid roleid, AclMode mode);
extern AclResult pg_largeobject_aclcheck_snapshot(Oid lang_oid, Oid roleid,
												  AclMode mode, Snapshot snapshot);
extern AclResult pg_namespace_aclcheck(Oid nsp_oid, Oid roleid, AclMode mode);
extern AclResult pg_tablespace_aclcheck(Oid spc_oid, Oid roleid, AclMode mode);
extern AclResult pg_foreign_data_wrapper_aclcheck(Oid fdw_oid, Oid roleid, AclMode mode);
extern AclResult pg_foreign_server_aclcheck(Oid srv_oid, Oid roleid, AclMode mode);
extern AclResult pg_type_aclcheck(Oid type_oid, Oid roleid, AclMode mode);

extern void aclcheck_error(AclResult aclerr, ObjectType objtype,
						   const char *objectname);

extern void aclcheck_error_col(AclResult aclerr, ObjectType objtype,
							   const char *objectname, const char *colname);

extern void aclcheck_error_type(AclResult aclerr, Oid typeOid);

extern void recordExtObjInitPriv(Oid objoid, Oid classoid);
extern void removeExtObjInitPriv(Oid objoid, Oid classoid);


/* ownercheck routines just return true (owner) or false (not) */
extern bool pg_class_ownercheck(Oid class_oid, Oid roleid);
extern bool pg_type_ownercheck(Oid type_oid, Oid roleid);
extern bool pg_oper_ownercheck(Oid oper_oid, Oid roleid);
extern bool pg_proc_ownercheck(Oid proc_oid, Oid roleid);
extern bool pg_language_ownercheck(Oid lan_oid, Oid roleid);
extern bool pg_largeobject_ownercheck(Oid lobj_oid, Oid roleid);
extern bool pg_namespace_ownercheck(Oid nsp_oid, Oid roleid);
extern bool pg_tablespace_ownercheck(Oid spc_oid, Oid roleid);
extern bool pg_opclass_ownercheck(Oid opc_oid, Oid roleid);
extern bool pg_opfamily_ownercheck(Oid opf_oid, Oid roleid);
extern bool pg_database_ownercheck(Oid db_oid, Oid roleid);
extern bool pg_collation_ownercheck(Oid coll_oid, Oid roleid);
extern bool pg_conversion_ownercheck(Oid conv_oid, Oid roleid);
extern bool pg_ts_dict_ownercheck(Oid dict_oid, Oid roleid);
extern bool pg_ts_config_ownercheck(Oid cfg_oid, Oid roleid);
extern bool pg_foreign_data_wrapper_ownercheck(Oid srv_oid, Oid roleid);
extern bool pg_foreign_server_ownercheck(Oid srv_oid, Oid roleid);
extern bool pg_event_trigger_ownercheck(Oid et_oid, Oid roleid);
extern bool pg_extension_ownercheck(Oid ext_oid, Oid roleid);
extern bool pg_publication_ownercheck(Oid pub_oid, Oid roleid);
extern bool pg_subscription_ownercheck(Oid sub_oid, Oid roleid);
extern bool pg_statistics_object_ownercheck(Oid stat_oid, Oid roleid);
extern bool has_createrole_privilege(Oid roleid);
extern bool has_bypassrls_privilege(Oid roleid);

/* Yugabyte table group feature. */
extern AclMode pg_tablegroup_aclmask(Oid grp_oid, Oid roleid,
									 AclMode mask, AclMaskHow how);
extern AclResult pg_tablegroup_aclcheck(Oid grp_oid, Oid roleid, AclMode mode);
extern bool pg_tablegroup_ownercheck(Oid grp_oid, Oid roleid);

#endif							/* ACL_H */
