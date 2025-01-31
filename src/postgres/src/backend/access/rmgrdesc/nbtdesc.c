/*-------------------------------------------------------------------------
 *
 * nbtdesc.c
 *	  rmgr descriptor routines for access/nbtree/nbtxlog.c
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/rmgrdesc/nbtdesc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/nbtxlog.h"

void
btree_desc(StringInfo buf, XLogReaderState *record)
{
	char	   *rec = XLogRecGetData(record);
	uint8		info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;

	switch (info)
	{
		case XLOG_BTREE_INSERT_LEAF:
		case XLOG_BTREE_INSERT_UPPER:
		case XLOG_BTREE_INSERT_META:
		case XLOG_BTREE_INSERT_POST:
			{
				xl_btree_insert *xlrec = (xl_btree_insert *) rec;

				appendStringInfo(buf, "off %u", xlrec->offnum);
				break;
			}
		case XLOG_BTREE_SPLIT_L:
		case XLOG_BTREE_SPLIT_R:
			{
				xl_btree_split *xlrec = (xl_btree_split *) rec;

				appendStringInfo(buf, "level %u, firstrightoff %d, newitemoff %d, postingoff %d",
								 xlrec->level, xlrec->firstrightoff,
								 xlrec->newitemoff, xlrec->postingoff);
				break;
			}
		case XLOG_BTREE_DEDUP:
			{
				xl_btree_dedup *xlrec = (xl_btree_dedup *) rec;

				appendStringInfo(buf, "nintervals %u", xlrec->nintervals);
				break;
			}
		case XLOG_BTREE_VACUUM:
			{
				xl_btree_vacuum *xlrec = (xl_btree_vacuum *) rec;

				appendStringInfo(buf, "ndeleted %u; nupdated %u",
								 xlrec->ndeleted, xlrec->nupdated);
				break;
			}
		case XLOG_BTREE_DELETE:
			{
				xl_btree_delete *xlrec = (xl_btree_delete *) rec;

				appendStringInfo(buf, "latestRemovedXid %u; ndeleted %u; nupdated %u",
								 xlrec->latestRemovedXid, xlrec->ndeleted, xlrec->nupdated);
				break;
			}
		case XLOG_BTREE_MARK_PAGE_HALFDEAD:
			{
				xl_btree_mark_page_halfdead *xlrec = (xl_btree_mark_page_halfdead *) rec;

				appendStringInfo(buf, "topparent %u; leaf %u; left %u; right %u",
								 xlrec->topparent, xlrec->leafblk, xlrec->leftblk, xlrec->rightblk);
				break;
			}
		case XLOG_BTREE_UNLINK_PAGE_META:
		case XLOG_BTREE_UNLINK_PAGE:
			{
				xl_btree_unlink_page *xlrec = (xl_btree_unlink_page *) rec;

				appendStringInfo(buf, "left %u; right %u; level %u; safexid %u:%u; ",
								 xlrec->leftsib, xlrec->rightsib, xlrec->level,
								 EpochFromFullTransactionId(xlrec->safexid),
								 XidFromFullTransactionId(xlrec->safexid));
				appendStringInfo(buf, "leafleft %u; leafright %u; leaftopparent %u",
								 xlrec->leafleftsib, xlrec->leafrightsib,
								 xlrec->leaftopparent);
				break;
			}
		case XLOG_BTREE_NEWROOT:
			{
				xl_btree_newroot *xlrec = (xl_btree_newroot *) rec;

				appendStringInfo(buf, "lev %u", xlrec->level);
				break;
			}
		case XLOG_BTREE_REUSE_PAGE:
			{
				xl_btree_reuse_page *xlrec = (xl_btree_reuse_page *) rec;

				appendStringInfo(buf, "rel %u/%u/%u; latestRemovedXid %u:%u",
								 xlrec->node.spcNode, xlrec->node.dbNode,
								 xlrec->node.relNode,
								 EpochFromFullTransactionId(xlrec->latestRemovedFullXid),
								 XidFromFullTransactionId(xlrec->latestRemovedFullXid));
				break;
			}
		case XLOG_BTREE_META_CLEANUP:
			{
				xl_btree_metadata *xlrec;

				xlrec = (xl_btree_metadata *) XLogRecGetBlockData(record, 0,
																  NULL);
				appendStringInfo(buf, "last_cleanup_num_delpages %u",
								 xlrec->last_cleanup_num_delpages);
				break;
			}
	}
}

const char *
btree_identify(uint8 info)
{
	const char *id = NULL;

	switch (info & ~XLR_INFO_MASK)
	{
		case XLOG_BTREE_INSERT_LEAF:
			id = "INSERT_LEAF";
			break;
		case XLOG_BTREE_INSERT_UPPER:
			id = "INSERT_UPPER";
			break;
		case XLOG_BTREE_INSERT_META:
			id = "INSERT_META";
			break;
		case XLOG_BTREE_SPLIT_L:
			id = "SPLIT_L";
			break;
		case XLOG_BTREE_SPLIT_R:
			id = "SPLIT_R";
			break;
		case XLOG_BTREE_INSERT_POST:
			id = "INSERT_POST";
			break;
		case XLOG_BTREE_DEDUP:
			id = "DEDUP";
			break;
		case XLOG_BTREE_VACUUM:
			id = "VACUUM";
			break;
		case XLOG_BTREE_DELETE:
			id = "DELETE";
			break;
		case XLOG_BTREE_MARK_PAGE_HALFDEAD:
			id = "MARK_PAGE_HALFDEAD";
			break;
		case XLOG_BTREE_UNLINK_PAGE:
			id = "UNLINK_PAGE";
			break;
		case XLOG_BTREE_UNLINK_PAGE_META:
			id = "UNLINK_PAGE_META";
			break;
		case XLOG_BTREE_NEWROOT:
			id = "NEWROOT";
			break;
		case XLOG_BTREE_REUSE_PAGE:
			id = "REUSE_PAGE";
			break;
		case XLOG_BTREE_META_CLEANUP:
			id = "META_CLEANUP";
			break;
	}

	return id;
}
