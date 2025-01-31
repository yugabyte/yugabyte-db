/*
 * contrib/btree_gist/btree_gist.h
 */
#ifndef __BTREE_GIST_H__
#define __BTREE_GIST_H__

#include "access/nbtree.h"
#include "fmgr.h"

#define BtreeGistNotEqualStrategyNumber 6

/* indexed types */

enum gbtree_type
{
	gbt_t_var,
	gbt_t_int2,
	gbt_t_int4,
	gbt_t_int8,
	gbt_t_float4,
	gbt_t_float8,
	gbt_t_numeric,
	gbt_t_ts,
	gbt_t_cash,
	gbt_t_oid,
	gbt_t_time,
	gbt_t_date,
	gbt_t_intv,
	gbt_t_macad,
	gbt_t_macad8,
	gbt_t_text,
	gbt_t_bpchar,
	gbt_t_bytea,
	gbt_t_bit,
	gbt_t_bool,
	gbt_t_inet,
	gbt_t_uuid,
	gbt_t_enum
};

#endif
