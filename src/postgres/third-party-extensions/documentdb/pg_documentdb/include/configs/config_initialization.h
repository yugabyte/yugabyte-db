/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/configs/config_initialization.h
 *
 * Common initialization of Mongo configs.
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTS_CONFIG_INITIALIZATION_H
#define DOCUMENTS_CONFIG_INITIALIZATION_H

void InitializeTestConfigurations(const char *prefix, const char *newGucPrefix);
void InitializeFeatureFlagConfigurations(const char *prefix, const char *newGucPrefix);
void InitializeBackgroundJobConfigurations(const char *prefix, const char *newGucPrefix);
void InitializeSystemConfigurations(const char *prefix, const char *newGucPrefix);

void InitDocumentDBBackgroundWorkerGucs(const char *prefix);
#endif
