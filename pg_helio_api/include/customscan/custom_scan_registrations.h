/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/custom_scan_registrations.h
 *
 *  Implementation of a custom scan plan.
 *
 *-------------------------------------------------------------------------
 */

#ifndef CUSTOM_SCAN_REGISTRATION_H
#define CUSTOM_SCAN_REGISTRATION_H

void RegisterScanNodes(void);
void RegisterQueryScanNodes(void);
void RegisterRumJoinScanNodes(void);

#endif
