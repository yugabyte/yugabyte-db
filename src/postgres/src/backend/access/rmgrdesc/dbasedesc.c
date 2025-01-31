/*-------------------------------------------------------------------------
 *
 * dbasedesc.c
 *	  rmgr descriptor routines for commands/dbcommands.c
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/rmgrdesc/dbasedesc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "commands/dbcommands_xlog.h"
#include "lib/stringinfo.h"


void
dbase_desc(StringInfo buf, XLogReaderState *record)
{
	char	   *rec = XLogRecGetData(record);
	uint8		info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;

	if (info == XLOG_DBASE_CREATE_FILE_COPY)
	{
		xl_dbase_create_file_copy_rec *xlrec =
		(xl_dbase_create_file_copy_rec *) rec;

		appendStringInfo(buf, "copy dir %u/%u to %u/%u",
						 xlrec->src_tablespace_id, xlrec->src_db_id,
						 xlrec->tablespace_id, xlrec->db_id);
	}
	else if (info == XLOG_DBASE_CREATE_WAL_LOG)
	{
		xl_dbase_create_wal_log_rec *xlrec =
		(xl_dbase_create_wal_log_rec *) rec;

		appendStringInfo(buf, "create dir %u/%u",
						 xlrec->tablespace_id, xlrec->db_id);
	}
	else if (info == XLOG_DBASE_DROP)
	{
		xl_dbase_drop_rec *xlrec = (xl_dbase_drop_rec *) rec;
		int			i;

		appendStringInfoString(buf, "dir");
		for (i = 0; i < xlrec->ntablespaces; i++)
			appendStringInfo(buf, " %u/%u",
							 xlrec->tablespace_ids[i], xlrec->db_id);
	}
}

const char *
dbase_identify(uint8 info)
{
	const char *id = NULL;

	switch (info & ~XLR_INFO_MASK)
	{
		case XLOG_DBASE_CREATE_FILE_COPY:
			id = "CREATE_FILE_COPY";
			break;
		case XLOG_DBASE_CREATE_WAL_LOG:
			id = "CREATE_WAL_LOG";
			break;
		case XLOG_DBASE_DROP:
			id = "DROP";
			break;
	}

	return id;
}
