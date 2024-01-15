/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/custom_scan_registrations.h
 *
 *  Implementation of a custom scan plan for PGMongo.
 *
 *-------------------------------------------------------------------------
 */

#ifndef CUSTOM_SCAN_REGISTRATION_H
#define CUSTOM_SCAN_REGISTRATION_H

void RegisterScanNodes(void);
void RegisterQueryScanNodes(void);

#endif
