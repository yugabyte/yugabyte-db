/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/relation_utils.c
 *
 * Utilities that operate on relation-like objects.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/fmgrprotos.h>

#include "metadata/relation_utils.h"


/*
 * SequenceGetNextValAsUser executes nextval_oid() for given sequence
 * as given user.
 */
Datum
SequenceGetNextValAsUser(Oid sequenceId, Oid userId)
{
	Oid savedUserId = InvalidOid;
	int savedSecurityContext = 0;
	GetUserIdAndSecContext(&savedUserId, &savedSecurityContext);
	SetUserIdAndSecContext(userId, SECURITY_LOCAL_USERID_CHANGE);

	Datum resultDatum = DirectFunctionCall1(nextval_oid, ObjectIdGetDatum(sequenceId));

	SetUserIdAndSecContext(savedUserId, savedSecurityContext);

	return resultDatum;
}
