/*
 * Copyright (c) YugaByte, Inc.
 */

package util

import (
	"os"
	"strings"

	"github.com/spf13/cobra"
)

// FeatureFlag string holds the defined feature flags that can be set
type FeatureFlag string

const (
	// TOOLS is a feature flag enabling doc generation
	TOOLS   FeatureFlag = "TOOLS"
	PREVIEW FeatureFlag = "PREVIEW"
)

func (f FeatureFlag) String() string {
	return string(f)
}

// IsFeatureFlagEnabled checks if feature flag is set in the env variable
func IsFeatureFlagEnabled(featureFlag FeatureFlag) bool {
	envVarName := "YBA_FF_" + featureFlag.String()
	return strings.ToLower(os.Getenv(envVarName)) == "true"
}

// AddCommandIfFeatureFlag adds the command to root if feature flag is enabled
func AddCommandIfFeatureFlag(rootCmd *cobra.Command, cmd *cobra.Command, featureFlag FeatureFlag) {
	// If the feature flag is enabled, add the command to the root command
	if IsFeatureFlagEnabled(featureFlag) {
		rootCmd.AddCommand(cmd)
	}
}
