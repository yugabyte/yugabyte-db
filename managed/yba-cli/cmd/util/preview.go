/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"github.com/spf13/cobra"
	"github.com/spf13/pflag"
	"github.com/spf13/viper"
)

// SetPreviewGroupID sets the preview group id to the sub command
func SetPreviewGroupID(cmd, subCmd *cobra.Command) {
	subCmd.GroupID = "preview"
}

// SetCommandAsPreview sets the command as preview
func SetCommandAsPreview(cmd, subCmd *cobra.Command) {
	AddCommandIfFeatureFlag(cmd, subCmd, PREVIEW)
}

// PreviewCommand adds the preview group to the root command
func PreviewCommand(cmd *cobra.Command, subCmds []*cobra.Command) {
	if IsFeatureFlagEnabled(PREVIEW) {
		cmd.AddGroup(&cobra.Group{
			ID:    "preview",
			Title: "Preview Commands (may change in future)",
		})
	}
	for _, subCmd := range subCmds {
		SetCommandAsPreview(cmd, subCmd)
		SetPreviewGroupID(cmd, subCmd)
	}
}

// PreviewFlag adds the preview flag to the command
func PreviewFlag(cmd *cobra.Command, flags *pflag.FlagSet, flagNames []string) {
	if IsFeatureFlagEnabled(PREVIEW) {
		if flags != nil {
			for _, flagName := range flagNames {
				flag := flags.Lookup(flagName)
				if flag != nil {
					flag.Usage = "This is a preview flag (may change in future). " + flag.Usage
					cmd.Flags().AddFlag(flag)
				}
			}
		}
	}
}

// PreviewFlagViperValue adds the preview flag to the command and binds to viper
func PreviewFlagViperValue(v *viper.Viper, cmd *cobra.Command, flagNames []string) {
	if IsFeatureFlagEnabled(PREVIEW) {
		for _, flagName := range flagNames {
			v.BindPFlag(flagName, cmd.Flags().Lookup(flagName))
		}
	}
}
