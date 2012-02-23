/*-------------------------------------------------------------------------
 *
 * pg_hint_plan.c
 *		Track statement execution in current/last transaction.
 *
 * Copyright (c) 2011, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  contrib/pg_hint_plan/pg_hint_plan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "nodes/print.h"
#include "utils/elog.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "optimizer/cost.h"
#include "optimizer/joininfo.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define HASH_ENTRIES 201

enum
{
	ENABLE_SEQSCAN		= 0x01,
	ENABLE_INDEXSCAN	= 0x02,
	ENABLE_BITMAPSCAN	= 0x04,
	ENABLE_TIDSCAN		= 0x08,
	ENABLE_NESTLOOP		= 0x10,
	ENABLE_MERGEJOIN	= 0x20,
	ENABLE_HASHJOIN		= 0x40
} TYPE_BITS;

typedef struct tidlist
{
	int nrels;
	Oid *oids;
} TidList;

typedef struct hash_entry
{
	TidList tidlist;
	unsigned char enforce_mask;
	struct hash_entry *next;
} HashEntry;

static HashEntry *hashent[HASH_ENTRIES];
#ifdef NOT_USED
static bool (*org_cost_hook)(CostHookType type, PlannerInfo *root, Path *path1, Path *path2);
#endif
static bool print_log = false;
static bool tweak_enabled = true;

/* Module callbacks */
void		_PG_init(void);
void		_PG_fini(void);
Datum		pg_add_hint(PG_FUNCTION_ARGS);
Datum		pg_clear_hint(PG_FUNCTION_ARGS);
Datum       pg_dump_hint(PG_FUNCTION_ARGS);
Datum		pg_enable_hint(bool arg, bool *isnull);
Datum		pg_enable_log(bool arg, bool *isnull);

#ifdef NOT_USED
static char *rels_str(PlannerInfo *root, Path *path);
static void dump_rels(char *label, PlannerInfo *root, Path *path, bool found, bool enabled);
static void dump_joinrels(char *label, PlannerInfo *root, Path *inpath, Path *outpath, bool found, bool enabled);
static bool my_cost_hook(CostHookType type, PlannerInfo *root, Path *path1, Path *path2);
#endif
static void free_hashent(HashEntry *head);
static unsigned int calc_hash(TidList *tidlist);
#ifdef NOT_USED
static HashEntry *search_ent(TidList *tidlist);
#endif

/* Join Method Hints */
typedef struct RelIdInfo
{
	Index		relid;
	Oid			oid;
	Alias	   *eref;
} RelIdInfo;

typedef struct JoinHint
{
	int				nrels;
	List		   *relidinfos;
	Relids			joinrelids;
	unsigned char	enforce_mask;
} JoinHint;

typedef struct GucVariables
{
	bool	enable_seqscan;
	bool	enable_indexscan;
	bool	enable_bitmapscan;
	bool	enable_tidscan;
	bool	enable_sort;
	bool	enable_hashagg;
	bool	enable_nestloop;
	bool	enable_material;
	bool	enable_mergejoin;
	bool	enable_hashjoin;
} GucVariables;

static void backup_guc(GucVariables *backup);
static void restore_guc(GucVariables *backup);
static void set_guc(unsigned char enforce_mask);
static void build_join_hints(PlannerInfo *root, int level, List *initial_rels);
static RelOptInfo *my_make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2);
static RelOptInfo *my_join_search(PlannerInfo *root, int levels_needed,
							  List *initial_rels);
static void my_join_search_one_level(PlannerInfo *root, int level);
static void make_rels_by_clause_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static void make_rels_by_clauseless_joins(PlannerInfo *root, RelOptInfo *old_rel, ListCell *other_rels);
static bool has_join_restriction(PlannerInfo *root, RelOptInfo *rel);

static join_search_hook_type org_join_search = NULL;
static List **join_hint_level = NULL;

PG_FUNCTION_INFO_V1(pg_add_hint);
PG_FUNCTION_INFO_V1(pg_clear_hint);

/*
 * Module load callbacks
 */
void
_PG_init(void)
{
	int i;

#ifdef NOT_USED
	org_cost_hook = cost_hook;
	cost_hook = my_cost_hook;
#endif
	org_join_search = join_search_hook;
	join_search_hook = my_join_search;
	
	for (i = 0 ; i < HASH_ENTRIES ; i++)
		hashent[i] = NULL;
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	int i;

#ifdef NOT_USED
	cost_hook = org_cost_hook;
#endif
	join_search_hook = org_join_search;

	for (i = 0 ; i < HASH_ENTRIES ; i++)
	{
		free_hashent(hashent[i]);
		hashent[i] = NULL;

	}
}

#ifdef NOT_USED
char *rels_str(PlannerInfo *root, Path *path)
{
	char buf[4096];								
	int relid;
	int first = 1;
	Bitmapset *tmpbms;

	if (path->pathtype == T_Invalid) return strdup("");

	tmpbms = bms_copy(path->parent->relids);

	buf[0] = 0;
	while ((relid = bms_first_member(tmpbms)) >= 0)
	{
		char idbuf[8];
		snprintf(idbuf, sizeof(idbuf), first ? "%d" : ", %d",
				 root->simple_rte_array[relid]->relid);
		if (strlen(buf) + strlen(idbuf) < sizeof(buf))
			strcat(buf, idbuf);
		first = 0;
	}

	return strdup(buf);
}
#endif

static int oidsortcmp(const void *a, const void *b)
{
	const Oid oida = *((const Oid *)a);
	const Oid oidb = *((const Oid *)b);

	return oida - oidb;
}

#ifdef NOT_USED
static TidList *maketidlist(PlannerInfo *root, Path *path1, Path *path2)
{
	int relid;
	Path *paths[2] = {path1, path2};
	int i;
	int j = 0;
	int nrels = 0;
	TidList *ret = (TidList *)malloc(sizeof(TidList));

	for (i = 0 ; i < 2 ; i++)
	{
		if (paths[i] != NULL)
			nrels += bms_num_members(paths[i]->parent->relids);
	}

	ret->nrels = nrels;
	ret->oids = (Oid *)malloc(nrels * sizeof(Oid));

	for (i = 0 ; i < 2 ; i++)
	{
		Bitmapset *tmpbms;

		if (paths[i] == NULL) continue;

		tmpbms= bms_copy(paths[i]->parent->relids);

		while ((relid = bms_first_member(tmpbms)) >= 0)
			ret->oids[j++] = root->simple_rte_array[relid]->relid;
	}

	if (nrels > 1)
		qsort(ret->oids, nrels, sizeof(Oid), oidsortcmp);

	return ret;
}

static void free_tidlist(TidList *tidlist)
{
	if (tidlist)
	{
		if (tidlist->oids)
			free(tidlist->oids);
		free(tidlist);
	}
}

int r = 0;
static void dump_rels(char *label, PlannerInfo *root, Path *path, bool found, bool enabled)
{
	char *relsstr;

	if (!print_log) return;
	relsstr = rels_str(root, path);
	ereport(INFO, (errmsg_internal("SCAN: %04d: %s for relation %s (%s, %s)\n",
								  r++, label, relsstr,
								  found ? "found" : "not found",
								  enabled ? "enabled" : "disabled")));
	free(relsstr);
}

int J = 0;
void dump_joinrels(char *label, PlannerInfo *root, Path *inpath, Path *outpath,
				   bool found, bool enabled)
{
	char *irelstr, *orelstr;

	if (!print_log) return;
	irelstr = rels_str(root, inpath);
	orelstr = rels_str(root, outpath);

	ereport(INFO, (errmsg_internal("JOIN: %04d: %s for relation ((%s),(%s)) (%s, %s)\n",
								  J++, label, irelstr, orelstr,
								  found ? "found" : "not found",
								  enabled ? "enabled" : "disabled")));
	free(irelstr);
	free(orelstr);
}


bool my_cost_hook(CostHookType type, PlannerInfo *root, Path *path1, Path *path2)
{
	TidList *tidlist;
	HashEntry *ent;
	bool ret = false;

	if (!tweak_enabled)
	{
		switch (type)
		{
			case COSTHOOK_seqscan:
				return enable_seqscan;
			case COSTHOOK_indexscan:
				return enable_indexscan;
			case COSTHOOK_bitmapscan:
				return enable_bitmapscan;
			case COSTHOOK_tidscan:
				return enable_tidscan;
			case COSTHOOK_nestloop:
				return enable_nestloop;
			case COSTHOOK_mergejoin:
				return enable_mergejoin;
			case COSTHOOK_hashjoin:
				return enable_hashjoin;
			default:
				ereport(LOG, (errmsg_internal("Unknown cost type")));
				break;
		}
	}
	switch (type)
	{
		case COSTHOOK_seqscan:
			tidlist = maketidlist(root, path1, path2);
			ent = search_ent(tidlist);
			free_tidlist(tidlist);
			ret = (ent ? (ent->enforce_mask & ENABLE_SEQSCAN) :
				   enable_seqscan);
			dump_rels("cost_seqscan", root, path1, ent != NULL, ret);
			return ret;
		case COSTHOOK_indexscan:
			tidlist = maketidlist(root, path1, path2);
			ent = search_ent(tidlist);
			free_tidlist(tidlist);
			ret = (ent ? (ent->enforce_mask & ENABLE_INDEXSCAN) :
				   enable_indexscan);
			dump_rels("cost_indexscan", root, path1, ent != NULL, ret);
			return ret;
		case COSTHOOK_bitmapscan:
			if (path1->pathtype != T_BitmapHeapScan)
			{
				ent = NULL;
				ret = enable_bitmapscan;
			}
			else
			{
				tidlist = maketidlist(root, path1, path2);
				ent = search_ent(tidlist);
				free_tidlist(tidlist);
				ret = (ent ? (ent->enforce_mask & ENABLE_BITMAPSCAN) :
					   enable_bitmapscan);
			}
			dump_rels("cost_bitmapscan", root, path1, ent != NULL, ret);

			return ret;
		case COSTHOOK_tidscan:
			tidlist = maketidlist(root, path1, path2);
			ent = search_ent(tidlist);
			free_tidlist(tidlist);
			ret = (ent ? (ent->enforce_mask & ENABLE_TIDSCAN) :
				   enable_tidscan);
			dump_rels("cost_tidscan", root, path1, ent != NULL, ret);
			return ret;
		case COSTHOOK_nestloop:
			tidlist = maketidlist(root, path1, path2);
			ent = search_ent(tidlist);
			free_tidlist(tidlist);
			ret = (ent ? (ent->enforce_mask & ENABLE_NESTLOOP) :
				   enable_nestloop);
			dump_joinrels("cost_nestloop", root, path1, path2,
						  ent != NULL, ret);
			return ret;
		case COSTHOOK_mergejoin:
			tidlist = maketidlist(root, path1, path2);
			ent = search_ent(tidlist);
			free_tidlist(tidlist);
			ret = (ent ? (ent->enforce_mask & ENABLE_MERGEJOIN) :
				   enable_mergejoin);
			dump_joinrels("cost_mergejoin", root, path1, path2,
						  ent != NULL, ret);
			return ret;
		case COSTHOOK_hashjoin:
			tidlist = maketidlist(root, path1, path2);
			ent = search_ent(tidlist);
			free_tidlist(tidlist);
			ret = (ent ? (ent->enforce_mask & ENABLE_HASHJOIN) :
				   enable_hashjoin);
			dump_joinrels("cost_hashjoin", root, path1, path2,
						  ent != NULL, ret);
			return ret;
		default:
			ereport(LOG, (errmsg_internal("Unknown cost type")));
			break;
	}
	
	return true;
}
#endif

static void free_hashent(HashEntry *head)
{
	HashEntry *next = head;

	while (next)
	{
		HashEntry *last = next;
		if (next->tidlist.oids != NULL) free(next->tidlist.oids);
		next = next->next;
		free(last);
	}
}

static HashEntry *parse_tidlist(char **str)
{
	char tidstr[8];
	char *p0;
	Oid tid[20]; /* ^^; */
	int ntids = 0;
	int i, len;
	HashEntry *ret;

	while (isdigit(**str) && ntids < 20)
	{
		p0 = *str;
		while (isdigit(**str)) (*str)++;
		len = *str - p0;
		if (len >= 8) return NULL;
		strncpy(tidstr, p0, len);
		tidstr[len] = 0;
		
		/* Tis 0 is valid? I don't know :-p */
		if ((tid[ntids++] = atoi(tidstr)) == 0) return NULL;

		if (**str == ',') (*str)++;
	}

	if (ntids > 1)
		qsort(tid, ntids, sizeof(Oid), oidsortcmp);
	ret = (HashEntry*)malloc(sizeof(HashEntry));
	ret->next = NULL;
	ret->enforce_mask = 0;
	ret->tidlist.nrels = ntids;
	ret->tidlist.oids = (Oid *)malloc(ntids * sizeof(Oid));
	for (i = 0 ; i < ntids ; i++)
		ret->tidlist.oids[i] = tid[i];
	return ret;	
}

static int parse_phrase(HashEntry **head, char **str)
{
	char *cmds[]	= {"seq", "index", "nest", "merge", "hash", NULL};
	unsigned char masks[] = {ENABLE_SEQSCAN, ENABLE_INDEXSCAN|ENABLE_BITMAPSCAN,
						   ENABLE_NESTLOOP, ENABLE_MERGEJOIN, ENABLE_HASHJOIN};
	char req[12];
	int cmd;
	HashEntry *ent = NULL;
	char *p0;
	int len;
	bool	not_use = false;

	p0 = *str;
	while (isalpha(**str) || **str == '_') (*str)++;
	len = *str - p0;
	if (**str != '(' || len >= 12) return 0;
	strncpy(req, p0, len);
	req[len] = 0;
	if (strncmp("no_", req, 3) == 0)
	{
		not_use = true;
		memmove(req, req + 3, len - 3 + 1);
	}
	for (cmd = 0 ; cmds[cmd] && strcmp(cmds[cmd], req) ; cmd++);
	if (cmds[cmd] == NULL) return 0;
	(*str)++;
	if ((ent = parse_tidlist(str)) == NULL) return 0;
	if (*(*str)++ != ')') return 0;
	if (**str != 0 && **str != ';') return 0;
	if (**str == ';') (*str)++;
	ent->enforce_mask = not_use ? ~masks[cmd] : masks[cmd];
	ent->next = NULL;
	*head = ent;

	return 1;
}


static HashEntry* parse_top(char* str)
{
	HashEntry *head = NULL;
	HashEntry *ent = NULL;

	if (!parse_phrase(&head, &str))
	{
		free_hashent(head);
		return NULL;
	}
	ent = head;

	while (*str)
	{
		if (!parse_phrase(&ent->next, &str))
		{
			free_hashent(head);
			return NULL;
		}
		ent = ent->next;
	}

	return head;
}

static bool ent_matches(TidList *key, HashEntry *ent2)
{
	int i;

	if (key->nrels != ent2->tidlist.nrels)
		return 0;

	for (i = 0 ; i < key->nrels ; i++)
		if (key->oids[i] != ent2->tidlist.oids[i])
			return 0;

	return 1;
}

static unsigned int calc_hash(TidList *tidlist)
{
	unsigned int hash = 0;
	int i = 0;
	
	for (i = 0 ; i < tidlist->nrels ; i++)
	{
		int j = 0;
		for (j = 0 ; j < sizeof(Oid) ; j++)
			hash = hash * 2 + ((tidlist->oids[i] >> (j * 8)) & 0xff);
	}

	return hash % HASH_ENTRIES;
;
}

#ifdef NOT_USED
static HashEntry *search_ent(TidList *tidlist)
{
	HashEntry *ent;
	if (tidlist == NULL) return NULL;

	ent = hashent[calc_hash(tidlist)];
	while(ent)
	{
		if (ent_matches(tidlist, ent))
			return ent;
		ent = ent->next;
	}

	return NULL;
}
#endif

Datum
pg_add_hint(PG_FUNCTION_ARGS)
{
	HashEntry *ret = NULL;
	char *str = NULL;
	int i = 0;

	if (PG_NARGS() < 1)
		ereport(ERROR, (errmsg_internal("No argument")));

	str = text_to_cstring(PG_GETARG_TEXT_PP(0));

	ret = parse_top(str);

	if (ret == NULL)
		ereport(ERROR, (errmsg_internal("Parse Error")));

	while (ret)
	{
		HashEntry *etmp = NULL;
		HashEntry *next = NULL;
		int hash = calc_hash(&ret->tidlist);
		while (hashent[hash] && ent_matches(&ret->tidlist, hashent[hash]))
		{
			etmp = hashent[hash]->next;
			hashent[hash]->next = NULL;
			free_hashent(hashent[hash]);
			hashent[hash] = etmp;

		}
		etmp = hashent[hash];
		while (etmp && etmp->next)
		{
			if (ent_matches(&ret->tidlist, etmp->next))
			{
				HashEntry *etmp2 = etmp->next->next;
				etmp->next->next = NULL;
				free_hashent(etmp->next);
				etmp->next = etmp2;
			} else
				etmp = etmp->next;
		}

		i++;
		next = ret->next;
		ret->next = hashent[hash];
		hashent[hash] = ret;
		ret = next;
	}
	PG_RETURN_INT32(i);
}

Datum
pg_clear_hint(PG_FUNCTION_ARGS)
{
	int i;
	int n = 0;

	for (i = 0 ; i < HASH_ENTRIES ; i++)
	{
		free_hashent(hashent[i]);
		hashent[i] = NULL;
		n++;

	}
	PG_RETURN_INT32(n);
}

Datum
pg_enable_hint(bool arg, bool *isnull)
{
	tweak_enabled = arg;
	PG_RETURN_INT32(0);
}

Datum
pg_enable_log(bool arg, bool *isnull)
{
	print_log = arg;
	PG_RETURN_INT32(0);
}

static int putsbuf(char **p, char *bottom, char *str)
{
	while (*p < bottom && *str)
	{
		*(*p)++ = *str++;
	}

	return (*str == 0);
}

static void dump_ent(HashEntry *ent, char **p, char *bottom)
{
	static char typesigs[] = "SIBTNMH";
	char sigs[sizeof(typesigs)];
	int i;

	if (!putsbuf(p, bottom, "[(")) return;
	for (i = 0 ; i < ent->tidlist.nrels ; i++)
	{
		if (i && !putsbuf(p, bottom, ", ")) return;
		if (*p >= bottom) return;
		*p += snprintf(*p, bottom - *p, "%d", ent->tidlist.oids[i]);
	}
	if (!putsbuf(p, bottom, "), ")) return;
	strcpy(sigs, typesigs);
	for (i = 0 ; i < 7 ; i++) /* Magic number here! */
	{
		if(((1<<i) & ent->enforce_mask) == 0)
			sigs[i] += 'a' - 'A';
	}
	if (!putsbuf(p, bottom, sigs)) return;
	if (!putsbuf(p, bottom, "]")) return;
}

Datum
pg_dump_hint(PG_FUNCTION_ARGS)
{
	char buf[16384]; /* ^^; */
	char *bottom = buf + sizeof(buf);
	char *p = buf;
	int i;
	int first = 1;

	memset(buf, 0, sizeof(buf));
	for (i = 0 ; i < HASH_ENTRIES ; i++)
	{
		if (hashent[i])
		{
			HashEntry *ent = hashent[i];
			while (ent)
			{
				if (first)
					first = 0;
				else
					putsbuf(&p, bottom, ", ");
				
				dump_ent(ent, &p, bottom);
				ent = ent->next;
			}
		}
	}
	if (p >= bottom) p--;
	*p = 0;
	
	PG_RETURN_CSTRING(cstring_to_text(buf));
}

/*
 * pg_add_hint()で登録した個別のヒントを、使用しやすい構造に変換する。
 */
static JoinHint *
set_relids(HashEntry *ent, RelIdInfo **relids, int nrels)
{
	int			i;
	int			j;
	JoinHint   *hint;

	hint = palloc(sizeof(JoinHint));
	hint->joinrelids = NULL;
	hint->relidinfos = NIL;

	for (i = 0; i < ent->tidlist.nrels; i++)
	{
		for (j = 0; j < nrels; j++)
		{
			if (ent->tidlist.oids[i] == relids[j]->oid)
			{
				hint->relidinfos = lappend(hint->relidinfos, relids[j]);
				hint->joinrelids =
					bms_add_member(hint->joinrelids, relids[j]->relid);
				break;
			}
		}

		if (j == nrels)
		{
			list_free(hint->relidinfos);
			pfree(hint);
			return NULL;
		}
	}

	hint->nrels = ent->tidlist.nrels;
	hint->enforce_mask = ent->enforce_mask;

	return hint;
}

/*
 * pg_add_hint()で登録したヒントから、今回のクエリで使用するもののみ抽出し、
 * 使用しやすい構造に変換する。
 */
static void
build_join_hints(PlannerInfo *root, int level, List *initial_rels)
{
	int			i;
	int			nrels;
	RelIdInfo **relids;
	JoinHint   *hint;

	relids = palloc(sizeof(RelIdInfo *) * root->simple_rel_array_size);

	// DEBUG  rtekind
	if (print_log)
	{
		ListCell   *l;
		foreach(l, initial_rels)
		{
			RelOptInfo *rel = (RelOptInfo *) lfirst(l);
			elog_node_display(INFO, "initial_rels", rel, true);
		}
		elog_node_display(INFO, "root", root, true);
		elog(INFO, "%s(simple_rel_array_size:%d, level:%d, query_level:%d, parent_root:%p)",
			__func__, root->simple_rel_array_size, level, root->query_level, root->parent_root);
	}

	for (i = 0, nrels = 0; i < root->simple_rel_array_size; i++)
	{
		if (root->simple_rel_array[i] == NULL)
			continue;

		relids[nrels] = palloc(sizeof(RelIdInfo));

		Assert(i == root->simple_rel_array[i]->relid);

		relids[nrels]->relid = i;
		relids[nrels]->oid = root->simple_rte_array[i]->relid;
		relids[nrels]->eref = root->simple_rte_array[i]->eref;
		//elog(INFO, "%d:%d:%d:%s", i, relids[nrels]->relid, relids[nrels]->oid, relids[nrels]->eref->aliasname);

		nrels++;
	}

	join_hint_level = palloc0(sizeof(List *) * (root->simple_rel_array_size));

	for (i = 0; i < HASH_ENTRIES; i++)
	{
		HashEntry *next;

		for (next = hashent[i]; next; next = next->next)
		{
			int	lv;
			if (!(next->enforce_mask & ENABLE_HASHJOIN) &&
				!(next->enforce_mask & ENABLE_NESTLOOP) &&
				!(next->enforce_mask & ENABLE_MERGEJOIN))
				continue;

			if ((hint = set_relids(next, relids, nrels)) == NULL)
				continue;

			lv = bms_num_members(hint->joinrelids);
			join_hint_level[lv] = lappend(join_hint_level[lv], hint);
		}
	}
}

static void
backup_guc(GucVariables *backup)
{
	backup->enable_seqscan = enable_seqscan;
	backup->enable_indexscan = enable_indexscan;
	backup->enable_bitmapscan = enable_bitmapscan;
	backup->enable_tidscan = enable_tidscan;
	backup->enable_sort = enable_sort;
	backup->enable_hashagg = enable_hashagg;
	backup->enable_nestloop = enable_nestloop;
	backup->enable_material = enable_material;
	backup->enable_mergejoin = enable_mergejoin;
	backup->enable_hashjoin = enable_hashjoin;
}

static void
restore_guc(GucVariables *backup)
{
	enable_seqscan = backup->enable_seqscan;
	enable_indexscan = backup->enable_indexscan;
	enable_bitmapscan = backup->enable_bitmapscan;
	enable_tidscan = backup->enable_tidscan;
	enable_sort = backup->enable_sort;
	enable_hashagg = backup->enable_hashagg;
	enable_nestloop = backup->enable_nestloop;
	enable_material = backup->enable_material;
	enable_mergejoin = backup->enable_mergejoin;
	enable_hashjoin = backup->enable_hashjoin;
}

static void
set_guc(unsigned char enforce_mask)
{
	enable_mergejoin = enforce_mask & ENABLE_MERGEJOIN ? true : false;
	enable_hashjoin = enforce_mask & ENABLE_HASHJOIN ? true : false;
	enable_nestloop = enforce_mask & ENABLE_NESTLOOP ? true : false;
}

/*
 * relidビットマスクと一致するヒントを探す
 */
static JoinHint *
find_join_hint(Relids joinrelids)
{
	List	   *join_hint;
	ListCell   *l;

	join_hint = join_hint_level[bms_num_members(joinrelids)];
	foreach(l, join_hint)
	{
		JoinHint   *hint = (JoinHint *) lfirst(l);
		if (bms_equal(joinrelids, hint->joinrelids))
			return hint;
	}

	return NULL;
}

/*
 * src/backend/optimizer/path/joinrels.c
 * export make_join_rel() をラップする関数
 * 
 * ヒントにしたがって、enabele_* パラメータを変更した上で、make_join_rel()を
 * 呼び出す。
 */
static RelOptInfo *
my_make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2)
{
	GucVariables	guc;
	Relids			joinrelids;
	JoinHint	   *hint;
	RelOptInfo	   *rel;

	joinrelids = bms_union(rel1->relids, rel2->relids);
	hint = find_join_hint(joinrelids);
	bms_free(joinrelids);

	if (hint)
	{
		backup_guc(&guc);
		set_guc(hint->enforce_mask);
	}

	rel = make_join_rel(root, rel1, rel2);

	if (hint)
		restore_guc(&guc);

	return rel;
}

/*
 * PostgreSQL 本体から流用した関数
 */

/*
 * src/backend/optimizer/path/allpaths.c
 * export standard_join_search() を流用
 * 
 * 変更箇所
 *  build_join_hints() の呼び出しを追加
 */
/*
 * standard_join_search
 *	  Find possible joinpaths for a query by successively finding ways
 *	  to join component relations into join relations.
 *
 * 'levels_needed' is the number of iterations needed, ie, the number of
 *		independent jointree items in the query.  This is > 1.
 *
 * 'initial_rels' is a list of RelOptInfo nodes for each independent
 *		jointree item.	These are the components to be joined together.
 *		Note that levels_needed == list_length(initial_rels).
 *
 * Returns the final level of join relations, i.e., the relation that is
 * the result of joining all the original relations together.
 * At least one implementation path must be provided for this relation and
 * all required sub-relations.
 *
 * To support loadable plugins that modify planner behavior by changing the
 * join searching algorithm, we provide a hook variable that lets a plugin
 * replace or supplement this function.  Any such hook must return the same
 * final join relation as the standard code would, but it might have a
 * different set of implementation paths attached, and only the sub-joinrels
 * needed for these paths need have been instantiated.
 *
 * Note to plugin authors: the functions invoked during standard_join_search()
 * modify root->join_rel_list and root->join_rel_hash.	If you want to do more
 * than one join-order search, you'll probably need to save and restore the
 * original states of those data structures.  See geqo_eval() for an example.
 */
static RelOptInfo *
my_join_search(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	int			lev;
	RelOptInfo *rel;

	/*
	 * This function cannot be invoked recursively within any one planning
	 * problem, so join_rel_level[] can't be in use already.
	 */
	Assert(root->join_rel_level == NULL);

	/*
	 * We employ a simple "dynamic programming" algorithm: we first find all
	 * ways to build joins of two jointree items, then all ways to build joins
	 * of three items (from two-item joins and single items), then four-item
	 * joins, and so on until we have considered all ways to join all the
	 * items into one rel.
	 *
	 * root->join_rel_level[j] is a list of all the j-item rels.  Initially we
	 * set root->join_rel_level[1] to represent all the single-jointree-item
	 * relations.
	 */
	root->join_rel_level = (List **) palloc0((levels_needed + 1) * sizeof(List *));

	root->join_rel_level[1] = initial_rels;

	build_join_hints(root, levels_needed, initial_rels);

	for (lev = 2; lev <= levels_needed; lev++)
	{
		ListCell   *lc;

		/*
		 * Determine all possible pairs of relations to be joined at this
		 * level, and build paths for making each one from every available
		 * pair of lower-level relations.
		 */
		my_join_search_one_level(root, lev);

		/*
		 * Do cleanup work on each just-processed rel.
		 */
		foreach(lc, root->join_rel_level[lev])
		{
			rel = (RelOptInfo *) lfirst(lc);

			/* Find and save the cheapest paths for this rel */
			set_cheapest(rel);

#ifdef OPTIMIZER_DEBUG
			debug_print_rel(root, rel);
#endif
		}
	}

	/*
	 * We should have a single rel at the final level.
	 */
	if (root->join_rel_level[levels_needed] == NIL)
		elog(ERROR, "failed to build any %d-way joins", levels_needed);
	Assert(list_length(root->join_rel_level[levels_needed]) == 1);

	rel = (RelOptInfo *) linitial(root->join_rel_level[levels_needed]);

	root->join_rel_level = NULL;

	return rel;
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static join_search_one_level() を流用
 * 
 * 変更箇所
 *  make_join_rel() の呼び出しをラップする、my_make_join_rel()の呼び出しに変更
 */
/*
 * join_search_one_level
 *	  Consider ways to produce join relations containing exactly 'level'
 *	  jointree items.  (This is one step of the dynamic-programming method
 *	  embodied in standard_join_search.)  Join rel nodes for each feasible
 *	  combination of lower-level rels are created and returned in a list.
 *	  Implementation paths are created for each such joinrel, too.
 *
 * level: level of rels we want to make this time
 * root->join_rel_level[j], 1 <= j < level, is a list of rels containing j items
 *
 * The result is returned in root->join_rel_level[level].
 */
static void
my_join_search_one_level(PlannerInfo *root, int level)
{
	List	  **joinrels = root->join_rel_level;
	ListCell   *r;
	int			k;

	Assert(joinrels[level] == NIL);

	/* Set join_cur_level so that new joinrels are added to proper list */
	root->join_cur_level = level;

	/*
	 * First, consider left-sided and right-sided plans, in which rels of
	 * exactly level-1 member relations are joined against initial relations.
	 * We prefer to join using join clauses, but if we find a rel of level-1
	 * members that has no join clauses, we will generate Cartesian-product
	 * joins against all initial rels not already contained in it.
	 *
	 * In the first pass (level == 2), we try to join each initial rel to each
	 * initial rel that appears later in joinrels[1].  (The mirror-image joins
	 * are handled automatically by make_join_rel.)  In later passes, we try
	 * to join rels of size level-1 from joinrels[level-1] to each initial rel
	 * in joinrels[1].
	 */
	foreach(r, joinrels[level - 1])
	{
		RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
		ListCell   *other_rels;

		if (level == 2)
			other_rels = lnext(r);		/* only consider remaining initial
										 * rels */
		else
			other_rels = list_head(joinrels[1]);		/* consider all initial
														 * rels */

		if (old_rel->joininfo != NIL || old_rel->has_eclass_joins ||
			has_join_restriction(root, old_rel))
		{
			/*
			 * Note that if all available join clauses for this rel require
			 * more than one other rel, we will fail to make any joins against
			 * it here.  In most cases that's OK; it'll be considered by
			 * "bushy plan" join code in a higher-level pass where we have
			 * those other rels collected into a join rel.
			 *
			 * See also the last-ditch case below.
			 */
			make_rels_by_clause_joins(root,
									  old_rel,
									  other_rels);
		}
		else
		{
			/*
			 * Oops, we have a relation that is not joined to any other
			 * relation, either directly or by join-order restrictions.
			 * Cartesian product time.
			 */
			make_rels_by_clauseless_joins(root,
										  old_rel,
										  other_rels);
		}
	}

	/*
	 * Now, consider "bushy plans" in which relations of k initial rels are
	 * joined to relations of level-k initial rels, for 2 <= k <= level-2.
	 *
	 * We only consider bushy-plan joins for pairs of rels where there is a
	 * suitable join clause (or join order restriction), in order to avoid
	 * unreasonable growth of planning time.
	 */
	for (k = 2;; k++)
	{
		int			other_level = level - k;

		/*
		 * Since make_join_rel(x, y) handles both x,y and y,x cases, we only
		 * need to go as far as the halfway point.
		 */
		if (k > other_level)
			break;

		foreach(r, joinrels[k])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
			ListCell   *other_rels;
			ListCell   *r2;

			/*
			 * We can ignore clauseless joins here, *except* when they
			 * participate in join-order restrictions --- then we might have
			 * to force a bushy join plan.
			 */
			if (old_rel->joininfo == NIL && !old_rel->has_eclass_joins &&
				!has_join_restriction(root, old_rel))
				continue;

			if (k == other_level)
				other_rels = lnext(r);	/* only consider remaining rels */
			else
				other_rels = list_head(joinrels[other_level]);

			for_each_cell(r2, other_rels)
			{
				RelOptInfo *new_rel = (RelOptInfo *) lfirst(r2);

				if (!bms_overlap(old_rel->relids, new_rel->relids))
				{
					/*
					 * OK, we can build a rel of the right level from this
					 * pair of rels.  Do so if there is at least one usable
					 * join clause or a relevant join restriction.
					 */
					if (have_relevant_joinclause(root, old_rel, new_rel) ||
						have_join_order_restriction(root, old_rel, new_rel))
					{
						(void) my_make_join_rel(root, old_rel, new_rel);
					}
				}
			}
		}
	}

	/*
	 * Last-ditch effort: if we failed to find any usable joins so far, force
	 * a set of cartesian-product joins to be generated.  This handles the
	 * special case where all the available rels have join clauses but we
	 * cannot use any of those clauses yet.  An example is
	 *
	 * SELECT * FROM a,b,c WHERE (a.f1 + b.f2 + c.f3) = 0;
	 *
	 * The join clause will be usable at level 3, but at level 2 we have no
	 * choice but to make cartesian joins.	We consider only left-sided and
	 * right-sided cartesian joins in this case (no bushy).
	 */
	if (joinrels[level] == NIL)
	{
		/*
		 * This loop is just like the first one, except we always call
		 * make_rels_by_clauseless_joins().
		 */
		foreach(r, joinrels[level - 1])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
			ListCell   *other_rels;

			if (level == 2)
				other_rels = lnext(r);	/* only consider remaining initial
										 * rels */
			else
				other_rels = list_head(joinrels[1]);	/* consider all initial
														 * rels */

			make_rels_by_clauseless_joins(root,
										  old_rel,
										  other_rels);
		}

		/*----------
		 * When special joins are involved, there may be no legal way
		 * to make an N-way join for some values of N.	For example consider
		 *
		 * SELECT ... FROM t1 WHERE
		 *	 x IN (SELECT ... FROM t2,t3 WHERE ...) AND
		 *	 y IN (SELECT ... FROM t4,t5 WHERE ...)
		 *
		 * We will flatten this query to a 5-way join problem, but there are
		 * no 4-way joins that join_is_legal() will consider legal.  We have
		 * to accept failure at level 4 and go on to discover a workable
		 * bushy plan at level 5.
		 *
		 * However, if there are no special joins then join_is_legal() should
		 * never fail, and so the following sanity check is useful.
		 *----------
		 */
		if (joinrels[level] == NIL && root->join_info_list == NIL)
			elog(ERROR, "failed to build any %d-way joins", level);
	}
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static make_rels_by_clause_joins() を流用
 * 
 * 変更箇所
 *  make_join_rel() の呼び出しをラップする、my_make_join_rel()の呼び出しに変更
 */
/*
 * make_rels_by_clause_joins
 *	  Build joins between the given relation 'old_rel' and other relations
 *	  that participate in join clauses that 'old_rel' also participates in
 *	  (or participate in join-order restrictions with it).
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * Note: at levels above 2 we will generate the same joined relation in
 * multiple ways --- for example (a join b) join c is the same RelOptInfo as
 * (b join c) join a, though the second case will add a different set of Paths
 * to it.  This is the reason for using the join_rel_level mechanism, which
 * automatically ensures that each new joinrel is only added to the list once.
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell in a linked list containing the other
 * rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it
 * will work for joining to joinrels too.
 */
static void
make_rels_by_clause_joins(PlannerInfo *root,
						  RelOptInfo *old_rel,
						  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(old_rel->relids, other_rel->relids) &&
			(have_relevant_joinclause(root, old_rel, other_rel) ||
			 have_join_order_restriction(root, old_rel, other_rel)))
		{
			(void) my_make_join_rel(root, old_rel, other_rel);
		}
	}
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static make_rels_by_clauseless_joins() を流用
 * 
 * 変更箇所
 *  make_join_rel() の呼び出しをラップする、my_make_join_rel()の呼び出しに変更
 */
/*
 * make_rels_by_clauseless_joins
 *	  Given a relation 'old_rel' and a list of other relations
 *	  'other_rels', create a join relation between 'old_rel' and each
 *	  member of 'other_rels' that isn't already included in 'old_rel'.
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell of a linked list containing the
 * other rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it would
 * work for joining to joinrels too.
 */
static void
make_rels_by_clauseless_joins(PlannerInfo *root,
							  RelOptInfo *old_rel,
							  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(other_rel->relids, old_rel->relids))
		{
			(void) my_make_join_rel(root, old_rel, other_rel);
		}
	}
}

/*
 * src/backend/optimizer/path/joinrels.c
 * static has_join_restriction() を流用
 * 
 * 変更箇所
 *  なし
 */
/*
 * has_join_restriction
 *		Detect whether the specified relation has join-order restrictions
 *		due to being inside an outer join or an IN (sub-SELECT).
 *
 * Essentially, this tests whether have_join_order_restriction() could
 * succeed with this rel and some other one.  It's OK if we sometimes
 * say "true" incorrectly.	(Therefore, we don't bother with the relatively
 * expensive has_legal_joinclause test.)
 */
static bool
has_join_restriction(PlannerInfo *root, RelOptInfo *rel)
{
	ListCell   *l;

	foreach(l, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(l);

		/* ignore full joins --- other mechanisms preserve their ordering */
		if (sjinfo->jointype == JOIN_FULL)
			continue;

		/* ignore if SJ is already contained in rel */
		if (bms_is_subset(sjinfo->min_lefthand, rel->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel->relids))
			continue;

		/* restricted if it overlaps LHS or RHS, but doesn't contain SJ */
		if (bms_overlap(sjinfo->min_lefthand, rel->relids) ||
			bms_overlap(sjinfo->min_righthand, rel->relids))
			return true;
	}

	return false;
}
