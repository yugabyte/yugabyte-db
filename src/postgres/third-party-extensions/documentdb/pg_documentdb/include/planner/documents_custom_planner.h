/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/documents_custom_planner.h
 *
 * Custom planning for Document APIs
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTS_CUSTOM_PLANNER_H
#define DOCUMENTS_CUSTOM_PLANNER_H

PlannedStmt * TryCreatePointReadPlan(Query *query);
#endif
