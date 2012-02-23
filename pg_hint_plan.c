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
#include "utils/elog.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "optimizer/cost.h"

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

int n = 0;
static void dump_rels(char *label, PlannerInfo *root, Path *path, bool found, bool enabled)
{
	char *relsstr;

	if (!print_log) return;
	relsstr = rels_str(root, path);
	ereport(LOG, (errmsg_internal("%04d: %s for relation %s (%s, %s)\n",
								  n++, label, relsstr,
								  found ? "found" : "not found",
								  enabled ? "enabled" : "disabled")));
	free(relsstr);
}

void dump_joinrels(char *label, PlannerInfo *root, Path *inpath, Path *outpath,
				   bool found, bool enabled)
{
	char *irelstr, *orelstr;

	if (!print_log) return;
	irelstr = rels_str(root, inpath);
	orelstr = rels_str(root, outpath);

	ereport(LOG, (errmsg_internal("%04d: %s for relation ((%s),(%s)) (%s, %s)\n",
								  n++, label, irelstr, orelstr,
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

	p0 = *str;
	while (isalpha(**str)) (*str)++;
	len = *str - p0;
	if (**str != '(' || len >= 12) return 0;
	strncpy(req, p0, len);
	req[len] = 0;
	for (cmd = 0 ; cmds[cmd] && strcmp(cmds[cmd], req) ; cmd++);
	if (cmds[cmd] == NULL) return 0;
	(*str)++;
	if ((ent = parse_tidlist(str)) == NULL) return 0;
	if (*(*str)++ != ')') return 0;
	if (**str != 0 && **str != ';') return 0;
	if (**str == ';') (*str)++;
	ent->enforce_mask = masks[cmd];
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
