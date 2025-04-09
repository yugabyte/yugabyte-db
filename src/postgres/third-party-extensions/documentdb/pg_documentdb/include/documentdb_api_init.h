/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/documentdb_api_init.h
 *
 * Exports related to shared library initialization for the API.
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_API_INIT_H
#define DOCUMENTDB_API_INIT_H

void InitApiConfigurations(char *prefix, char *newGucPrefix);
void InstallDocumentDBApiPostgresHooks(void);
void UninstallDocumentDBApiPostgresHooks(void);
void InitializeDocumentDBBackgroundWorker(char *libraryName, char *gucPrefix,
										  char *extensionObjectPrefix);
void InitializeSharedMemoryHooks(void);
#endif
