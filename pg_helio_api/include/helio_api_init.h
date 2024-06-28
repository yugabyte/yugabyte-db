/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/helio_api_init.h
 *
 * Exports related to shared library initialization for the API.
 *
 *-------------------------------------------------------------------------
 */
#ifndef HELIO_API_INIT_H
#define HELIO_API_INIT_H

void InitApiConfigurations(char *prefix);
void InstallHelioApiPostgresHooks(void);
void UninstallHelioApiPostgresHooks(void);
void InitializeSharedMemoryHooks(void);
#endif
