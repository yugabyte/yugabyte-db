/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/infrastructure/helio_external_configs.h
 *
 * User facing configuration options for pg_helio_api
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_EXTERNAL_CONFIGS_H
#define HELIO_EXTERNAL_CONFIGS_H

/*-------------------------------------------------------------------------
 * Data types
 *-------------------------------------------------------------------------
 */

/*
 * SettableProperties defines the properties of a configuration that can be set at startup or runtime.
 */
typedef struct SettableProperties
{
	/* isSettableAtStartup */
	bool isSettableAtStartup;

	/* isSettableAtRuntime */
	bool isSettableAtRuntime;
} SettableProperties;


/*
 * ConfigValueType defines the type of value that a configuration can hold.
 */
typedef enum ConfigValueType
{
	ConfigValueType_Invalid,
	ConfigValueType_Integer,
	ConfigValueType_Double,
	ConfigValueType_Bool,
	ConfigValueType_String
} ConfigValueType;


/*
 * ConfigValues defines the values that the configuration is holding.
 * Also defines the Min and Max values for integer and double types.
 */
typedef struct ConfigValues
{
	/* Pointer to runtime static variable holding the value for config */
	void *RuntimeValue;

	/* Default value for config at boot time. */
	union
	{
		int integerValue;
		double doubleValue;
		bool boolValue;
		char *stringValue;
	} DefaultValue;

	/* Minimum value used for integer and double based configs */
	union
	{
		int integerValue;
		double doubleValue;
	} MinValue;

/* Minimum value used for integer and double based configs */
	union
	{
		int integerValue;
		double doubleValue;
	} MaxValue;

	ConfigValueType valueType;
} ConfigValues;

typedef struct ExtensionExternalConfigInfo
{
	/* Name of the config. */
	const char *name;

	/* Long description of the config. */
	const char *description;

	/* Settable properties */
	SettableProperties settableProperties;

	/* Values */
	ConfigValues values;
} ExtensionExternalConfigInfo;

/* External Configs GUC's runtime variables */
extern bool PEC_FeatureFlagCollModIndexUnique;
extern bool PEC_FeatureFlagTimeseriesMetricIndexes;
extern int PEC_TransactionTimeoutLimitSeconds;
extern int32 PEC_InternalQueryMaxAllowedDensifyDocs;
extern int PEC_InternalDocumentSourceDensifyMaxMemoryBytes;

void InitializeExtensionExternalConfigs(char *prefix);
const ExtensionExternalConfigInfo * GetExtensionExternalConfigByName(const
																	 char *configName);
const ExtensionExternalConfigInfo * GetExtensionExternalConfigByIndex(int index);
int GetExtensionExternalConfigCount(void);
int GetIntegerValueFromConfig(const ExtensionExternalConfigInfo *config);
double GetDoubleValueFromConfig(const ExtensionExternalConfigInfo *config);
bool GetBoolValueFromConfig(const ExtensionExternalConfigInfo *config);
char * GetStringValueFromConfig(const ExtensionExternalConfigInfo *config);

#endif
