/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/infrastructure/external_configs.c
 *
 * This provides utilities to work with user facing configuration parameters for the current extension.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <utils/guc.h>

#include "infrastructure/helio_external_configs.h"

#define DEFAULT_FEATURE_FLAG_COLL_MOD_INDEX_UNIQUE false
bool PEC_FeatureFlagCollModIndexUnique = DEFAULT_FEATURE_FLAG_COLL_MOD_INDEX_UNIQUE;

#define DEFAULT_FEATURE_FLAG_TIMESERIES_METRICS_INDEX false
bool PEC_FeatureFlagTimeseriesMetricIndexes =
	DEFAULT_FEATURE_FLAG_TIMESERIES_METRICS_INDEX;

#define DEFAULT_TRANSACTION_TIMEOUT_LIMIT_SECONDS 30
int PEC_TransactionTimeoutLimitSeconds =
	DEFAULT_TRANSACTION_TIMEOUT_LIMIT_SECONDS;

bool PEC_FailurePointDisablePipelineOptimization = false;
bool PEC_InternalQueryForceClassicEngine = true;
bool PEC_InternalQueryEnableSlotBasedExecutionEngine = false;

/*
 * =================================================================================================================
 * Forward static declarations
 * =================================================================================================================
 */
static int CompareConfigurationName(const void *name, const void *config);


/*
 * Static list of external parameters that can be viewed by the external users, using getParameter command
 *
 * Note: Please maintain the order of configurations in this list as they are returned in the response of getParameter
 * as well as configuration are searched in the list using binary search.
 */
static const ExtensionExternalConfigInfo ExtensionExternalConfigurations[] = {
	/* failpoint.disablePipelineOptimization */
	{
		.name = "failpoint.disablePipelineOptimization",
		.description = "Internal feature flag for disabling pipeline optimizations.",
		.settableProperties = {
			.isSettableAtStartup = true,
			.isSettableAtRuntime = false
		},
		.values = {
			.RuntimeValue = (void *) &PEC_FailurePointDisablePipelineOptimization,
			.DefaultValue.boolValue = false,
			.valueType = ConfigValueType_Bool
		}
	},

	/* featureFlagCollModIndexUnique */
	{
		.name = "featureFlagCollModIndexUnique",
		.description =
			"Enables the feature flag for unique indexes on collection modification.",
		.settableProperties = {
			.isSettableAtStartup = true,
			.isSettableAtRuntime = false
		},
		.values = {
			.RuntimeValue = (void *) &PEC_FeatureFlagCollModIndexUnique,
			.DefaultValue.boolValue = false,
			.valueType = ConfigValueType_Bool
		}
	},

	/* featureFlagTimeseriesMetricIndexes */
	{
		.name = "featureFlagTimeseriesMetricIndexes",
		.description = "Enables the feature flag for timeseries metrics indexes.",
		.settableProperties = {
			.isSettableAtStartup = true,
			.isSettableAtRuntime = false
		},
		.values = {
			.RuntimeValue = (void *) &PEC_FeatureFlagTimeseriesMetricIndexes,
			.DefaultValue.boolValue = false,
			.valueType = ConfigValueType_Bool
		}
	},

	/* internalQueryEnableSlotBasedExecutionEngine */
	{
		.name = "internalQueryEnableSlotBasedExecutionEngine",
		.description = "Enables the feature flag for enabling classic query engine.",
		.settableProperties = {
			.isSettableAtStartup = true,
			.isSettableAtRuntime = false
		},
		.values = {
			.RuntimeValue = (void *) &PEC_InternalQueryEnableSlotBasedExecutionEngine,
			.DefaultValue.boolValue = true,
			.valueType = ConfigValueType_Bool
		}
	},

	/* internalQueryForceClassicEngine */
	{
		.name = "internalQueryForceClassicEngine",
		.description = "Enables the feature flag for enabling classic query engine.",
		.settableProperties = {
			.isSettableAtStartup = true,
			.isSettableAtRuntime = false
		},
		.values = {
			.RuntimeValue = (void *) &PEC_InternalQueryForceClassicEngine,
			.DefaultValue.boolValue = true,
			.valueType = ConfigValueType_Bool
		}
	},

	/* transactionLifetimeLimitSeconds */
	{
		.name = "transactionLifetimeLimitSeconds",
		.description =
			"Sets the transaction lifetime limit in seconds for GW postgres client.",
		.settableProperties = {
			.isSettableAtStartup = true,
			.isSettableAtRuntime = false
		},
		.values = {
			.RuntimeValue = (void *) &PEC_TransactionTimeoutLimitSeconds,
			.DefaultValue.integerValue = DEFAULT_TRANSACTION_TIMEOUT_LIMIT_SECONDS,

			/* For now max and min are default so that this can't be changed and pose problems for GW */
			.MinValue.integerValue = DEFAULT_TRANSACTION_TIMEOUT_LIMIT_SECONDS,
			.MaxValue.integerValue = DEFAULT_TRANSACTION_TIMEOUT_LIMIT_SECONDS,
			.valueType = ConfigValueType_Integer,
		}
	},
};
static int NumberOfExternalParameters = sizeof(ExtensionExternalConfigurations) /
										sizeof(ExtensionExternalConfigInfo);


/*
 * InitializeExtensionExternalConfigs initializes all the user facing configuration
 * parameters for the current extension.
 *
 * All the configurations defined here are prefixed with __API_GUC_PREFIX__ `.external` to categorize
 * them as user facing configurations. Only these will be returned in the response of
 * `getParameter` command.
 *
 */
void
InitializeExtensionExternalConfigs(char *prefix)
{
	for (int i = 0; i < NumberOfExternalParameters; i++)
	{
		const ExtensionExternalConfigInfo *config = &ExtensionExternalConfigurations[i];
		const char *prefixedName = psprintf("%s.external.%s",
											prefix, config->name);
		switch (config->values.valueType)
		{
			case ConfigValueType_Bool:
			{
				DefineCustomBoolVariable(
					prefixedName,
					gettext_noop(config->description),
					NULL, (bool *) config->values.RuntimeValue,
					config->values.DefaultValue.boolValue,
					PGC_SUSET, 0, NULL, NULL, NULL);
				break;
			}

			case ConfigValueType_Integer:
			{
				DefineCustomIntVariable(
					prefixedName,
					gettext_noop(config->description),
					NULL, (int *) config->values.RuntimeValue,
					config->values.DefaultValue.integerValue,
					config->values.MinValue.integerValue,
					config->values.MaxValue.integerValue,
					PGC_SUSET, 0, NULL, NULL, NULL);
				break;
			}

			case ConfigValueType_Double:
			{
				DefineCustomRealVariable(
					prefixedName,
					gettext_noop(config->description),
					NULL, (double *) config->values.RuntimeValue,
					config->values.DefaultValue.doubleValue,
					config->values.MinValue.doubleValue,
					config->values.MaxValue.doubleValue,
					PGC_SUSET, 0, NULL, NULL, NULL);
				break;
			}

			case ConfigValueType_String:
			{
				DefineCustomStringVariable(
					prefixedName,
					gettext_noop(config->description),
					NULL, (char **) config->values.RuntimeValue,
					config->values.DefaultValue.stringValue,
					PGC_SUSET, 0, NULL, NULL, NULL);
				break;
			}

			default:
			{
				ereport(ERROR, (errmsg(
									"Invalid extension external configs found: %s with type %d",
									config->name, config->values.valueType)));
			}
		}
		pfree((void *) prefixedName);
	}
}


/*
 * GetExtensionExternalConfig returns the configuration info for the given config name.
 * Returns NULL if the config is not found.
 */
const ExtensionExternalConfigInfo *
GetExtensionExternalConfigByName(const char *configName)
{
	const ExtensionExternalConfigInfo *configItem =
		(const ExtensionExternalConfigInfo *) bsearch(configName,
													  ExtensionExternalConfigurations,
													  NumberOfExternalParameters,
													  sizeof(ExtensionExternalConfigInfo),
													  CompareConfigurationName);
	return configItem;
}


/*
 * GetExtensionExternalConfigByIndex returns the configuration at particular index.
 * Returns NULL if the config is not found.
 */
const ExtensionExternalConfigInfo *
GetExtensionExternalConfigByIndex(int index)
{
	if (index < 0 || index >= NumberOfExternalParameters)
	{
		return NULL;
	}
	return (const ExtensionExternalConfigInfo *) &ExtensionExternalConfigurations[index];
}


/*
 * Returns number of external configs
 */
int
GetExtensionExternalConfigCount()
{
	return NumberOfExternalParameters;
}


/*
 * GetIntegerValueFromConfig returns the integer value from the integer config.
 */
int
GetIntegerValueFromConfig(const ExtensionExternalConfigInfo *config)
{
	Assert(config->values.valueType == ConfigValueType_Integer);
	return *((int *) config->values.RuntimeValue);
}


/*
 * GetDoubleValueFromConfig returns the double value from the double config.
 */
double
GetDoubleValueFromConfig(const ExtensionExternalConfigInfo *config)
{
	Assert(config->values.valueType == ConfigValueType_Double);
	return *((double *) config->values.RuntimeValue);
}


/*
 * GetBoolValueFromConfig returns the bool value from the bool config.
 */
bool
GetBoolValueFromConfig(const ExtensionExternalConfigInfo *config)
{
	Assert(config->values.valueType == ConfigValueType_Bool);
	return *((bool *) config->values.RuntimeValue);
}


/*
 * GetStringValueFromConfig returns the string value from the string config.
 */
char *
GetStringValueFromConfig(const ExtensionExternalConfigInfo *config)
{
	Assert(config->values.valueType == ConfigValueType_String);
	return *((char **) config->values.RuntimeValue);
}


/*
 * Compares the config name with the name passed as key
 */
static int
CompareConfigurationName(const void *name, const void *config)
{
	return strcmp((char *) name, ((const ExtensionExternalConfigInfo *) config)->name);
}
