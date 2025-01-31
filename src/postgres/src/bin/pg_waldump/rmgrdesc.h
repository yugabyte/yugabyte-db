/*
 * rmgrdesc.h
 *
 * pg_waldump resource managers declaration
 *
 * src/bin/pg_waldump/rmgrdesc.h
 */
#ifndef RMGRDESC_H
#define RMGRDESC_H

#include "access/xlogreader.h"
#include "lib/stringinfo.h"

typedef struct RmgrDescData
{
	const char *rm_name;
	void		(*rm_desc) (StringInfo buf, XLogReaderState *record);
	const char *(*rm_identify) (uint8 info);
} RmgrDescData;

extern const RmgrDescData *GetRmgrDesc(RmgrId rmid);

#endif							/* RMGRDESC_H */
