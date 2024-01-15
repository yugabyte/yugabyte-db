/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/relation_utils.h
 *
 * Utilities that operate on relation-like objects.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#ifndef RELATION_UTILS_H
#define RELATION_UTILS_H

Datum SequenceGetNextValAsUser(Oid sequenceId, Oid userId);

#endif
