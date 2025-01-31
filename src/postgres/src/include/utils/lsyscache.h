/*-------------------------------------------------------------------------
 *
 * lsyscache.h
 *	  Convenience routines for common queries in the system catalog cache.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/lsyscache.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef LSYSCACHE_H
#define LSYSCACHE_H

#include "access/attnum.h"
#include "access/htup.h"
#include "nodes/pg_list.h"

/* avoid including subscripting.h here */
struct SubscriptRoutines;

/* Result list element for get_op_btree_interpretation */
typedef struct OpBtreeInterpretation
{
	Oid			opfamily_id;	/* btree opfamily containing operator */
	int			strategy;		/* its strategy number */
	Oid			oplefttype;		/* declared left input datatype */
	Oid			oprighttype;	/* declared right input datatype */
} OpBtreeInterpretation;

/* I/O function selector for get_type_io_data */
typedef enum IOFuncSelector
{
	IOFunc_input,
	IOFunc_output,
	IOFunc_receive,
	IOFunc_send
} IOFuncSelector;

/* Flag bits for get_attstatsslot */
#define ATTSTATSSLOT_VALUES		0x01
#define ATTSTATSSLOT_NUMBERS	0x02

/* Result struct for get_attstatsslot */
typedef struct AttStatsSlot
{
	/* Always filled: */
	Oid			staop;			/* Actual staop for the found slot */
	Oid			stacoll;		/* Actual collation for the found slot */
	/* Filled if ATTSTATSSLOT_VALUES is specified: */
	Oid			valuetype;		/* Actual datatype of the values */
	Datum	   *values;			/* slot's "values" array, or NULL if none */
	int			nvalues;		/* length of values[], or 0 */
	/* Filled if ATTSTATSSLOT_NUMBERS is specified: */
	float4	   *numbers;		/* slot's "numbers" array, or NULL if none */
	int			nnumbers;		/* length of numbers[], or 0 */

	/* Remaining fields are private to get_attstatsslot/free_attstatsslot */
	void	   *values_arr;		/* palloc'd values array, if any */
	void	   *numbers_arr;	/* palloc'd numbers array, if any */
} AttStatsSlot;

/* Hook for plugins to get control in get_attavgwidth() */
typedef int32 (*get_attavgwidth_hook_type) (Oid relid, AttrNumber attnum);
extern PGDLLIMPORT get_attavgwidth_hook_type get_attavgwidth_hook;

extern bool op_in_opfamily(Oid opno, Oid opfamily);
extern int	get_op_opfamily_strategy(Oid opno, Oid opfamily);
extern Oid	get_op_opfamily_sortfamily(Oid opno, Oid opfamily);
extern void get_op_opfamily_properties(Oid opno, Oid opfamily, bool ordering_op,
									   int *strategy,
									   Oid *lefttype,
									   Oid *righttype);
extern Oid	get_opfamily_member(Oid opfamily, Oid lefttype, Oid righttype,
								int16 strategy);
extern bool get_ordering_op_properties(Oid opno,
									   Oid *opfamily, Oid *opcintype, int16 *strategy);
extern Oid	get_equality_op_for_ordering_op(Oid opno, bool *reverse);
extern Oid	get_ordering_op_for_equality_op(Oid opno, bool use_lhs_type);
extern List *get_mergejoin_opfamilies(Oid opno);
extern bool get_compatible_hash_operators(Oid opno,
										  Oid *lhs_opno, Oid *rhs_opno);
extern bool get_op_hash_functions(Oid opno,
								  RegProcedure *lhs_procno, RegProcedure *rhs_procno);
extern List *get_op_btree_interpretation(Oid opno);
extern bool equality_ops_are_compatible(Oid opno1, Oid opno2);
extern bool comparison_ops_are_compatible(Oid opno1, Oid opno2);
extern Oid	get_opfamily_proc(Oid opfamily, Oid lefttype, Oid righttype,
							  int16 procnum);
extern char *get_attname(Oid relid, AttrNumber attnum, bool missing_ok);
extern AttrNumber get_attnum(Oid relid, const char *attname);
extern int	get_attstattarget(Oid relid, AttrNumber attnum);
extern char get_attgenerated(Oid relid, AttrNumber attnum);
extern Oid	get_atttype(Oid relid, AttrNumber attnum);
extern void get_atttypetypmodcoll(Oid relid, AttrNumber attnum,
								  Oid *typid, int32 *typmod, Oid *collid);
extern Datum get_attoptions(Oid relid, int16 attnum);
extern Oid	get_cast_oid(Oid sourcetypeid, Oid targettypeid, bool missing_ok);
extern char *get_collation_name(Oid colloid);
extern bool get_collation_isdeterministic(Oid colloid);
extern char *get_constraint_name(Oid conoid);
extern Oid	get_constraint_index(Oid conoid);
extern char *get_language_name(Oid langoid, bool missing_ok);
extern Oid	get_opclass_family(Oid opclass);
extern Oid	get_opclass_input_type(Oid opclass);
extern bool get_opclass_opfamily_and_input_type(Oid opclass,
												Oid *opfamily, Oid *opcintype);
extern RegProcedure get_opcode(Oid opno);
extern char *get_opname(Oid opno);
extern Oid	get_op_rettype(Oid opno);
extern void op_input_types(Oid opno, Oid *lefttype, Oid *righttype);
extern bool op_mergejoinable(Oid opno, Oid inputtype);
extern bool op_hashjoinable(Oid opno, Oid inputtype);
extern bool op_strict(Oid opno);
extern char op_volatile(Oid opno);
extern Oid	get_commutator(Oid opno);
extern Oid	get_negator(Oid opno);
extern RegProcedure get_oprrest(Oid opno);
extern RegProcedure get_oprjoin(Oid opno);
extern char *get_func_name(Oid funcid);
extern Oid	get_func_namespace(Oid funcid);
extern Oid	get_func_rettype(Oid funcid);
extern int	get_func_nargs(Oid funcid);
extern Oid	get_func_signature(Oid funcid, Oid **argtypes, int *nargs);
extern Oid	get_func_variadictype(Oid funcid);
extern bool get_func_retset(Oid funcid);
extern bool func_strict(Oid funcid);
extern char func_volatile(Oid funcid);
extern char func_parallel(Oid funcid);
extern char get_func_prokind(Oid funcid);
extern bool get_func_leakproof(Oid funcid);
extern RegProcedure get_func_support(Oid funcid);
extern Oid	get_relname_relid(const char *relname, Oid relnamespace);
extern char *get_rel_name(Oid relid);
extern Oid	get_rel_namespace(Oid relid);
extern Oid	get_rel_type_id(Oid relid);
extern char get_rel_relkind(Oid relid);
extern bool get_rel_relispartition(Oid relid);
extern Oid	get_rel_tablespace(Oid relid);
extern char get_rel_persistence(Oid relid);
extern Oid	get_transform_fromsql(Oid typid, Oid langid, List *trftypes);
extern Oid	get_transform_tosql(Oid typid, Oid langid, List *trftypes);
extern bool get_typisdefined(Oid typid);
extern int16 get_typlen(Oid typid);
extern bool get_typbyval(Oid typid);
extern void get_typlenbyval(Oid typid, int16 *typlen, bool *typbyval);
extern void get_typlenbyvalalign(Oid typid, int16 *typlen, bool *typbyval,
								 char *typalign);
extern Oid	getTypeIOParam(HeapTuple typeTuple);
extern void get_type_io_data(Oid typid,
							 IOFuncSelector which_func,
							 int16 *typlen,
							 bool *typbyval,
							 char *typalign,
							 char *typdelim,
							 Oid *typioparam,
							 Oid *func);
extern char get_typstorage(Oid typid);
extern Node *get_typdefault(Oid typid);
extern char get_typtype(Oid typid);
extern bool type_is_rowtype(Oid typid);
extern bool type_is_enum(Oid typid);
extern bool type_is_range(Oid typid);
extern bool type_is_multirange(Oid typid);
extern void get_type_category_preferred(Oid typid,
										char *typcategory,
										bool *typispreferred);
extern Oid	get_typ_typrelid(Oid typid);
extern Oid	get_element_type(Oid typid);
extern Oid	get_array_type(Oid typid);
extern Oid	get_promoted_array_type(Oid typid);
extern Oid	get_base_element_type(Oid typid);
extern void getTypeInputInfo(Oid type, Oid *typInput, Oid *typIOParam);
extern void getTypeOutputInfo(Oid type, Oid *typOutput, bool *typIsVarlena);
extern void getTypeBinaryInputInfo(Oid type, Oid *typReceive, Oid *typIOParam);
extern void getTypeBinaryOutputInfo(Oid type, Oid *typSend, bool *typIsVarlena);
extern Oid	get_typmodin(Oid typid);
extern Oid	get_typcollation(Oid typid);
extern bool type_is_collatable(Oid typid);
extern RegProcedure get_typsubscript(Oid typid, Oid *typelemp);
extern const struct SubscriptRoutines *getSubscriptingRoutines(Oid typid,
															   Oid *typelemp);
extern Oid	getBaseType(Oid typid);
extern Oid	getBaseTypeAndTypmod(Oid typid, int32 *typmod);
extern int32 get_typavgwidth(Oid typid, int32 typmod);
extern float4 yb_get_attdistinctcount(Oid relid, AttrNumber attnum);
extern int32 get_attavgwidth(Oid relid, AttrNumber attnum);
extern bool get_attstatsslot(AttStatsSlot *sslot, HeapTuple statstuple,
							 int reqkind, Oid reqop, int flags);
extern void free_attstatsslot(AttStatsSlot *sslot);
extern char *get_namespace_name(Oid nspid);
extern char *get_namespace_name_or_temp(Oid nspid);
extern Oid	get_range_subtype(Oid rangeOid);
extern Oid	get_range_collation(Oid rangeOid);
extern Oid	get_range_multirange(Oid rangeOid);
extern Oid	get_multirange_range(Oid multirangeOid);
extern Oid	get_index_column_opclass(Oid index_oid, int attno);
extern bool get_index_isreplident(Oid index_oid);
extern bool get_index_isvalid(Oid index_oid);
extern bool get_index_isclustered(Oid index_oid);

#define type_is_array(typid)  (get_element_type(typid) != InvalidOid)
/* type_is_array_domain accepts both plain arrays and domains over arrays */
#define type_is_array_domain(typid)  (get_base_element_type(typid) != InvalidOid)

#define TypeIsToastable(typid)	(get_typstorage(typid) != TYPSTORAGE_PLAIN)

#endif							/* LSYSCACHE_H */
