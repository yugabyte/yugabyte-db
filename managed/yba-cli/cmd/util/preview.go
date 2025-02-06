/*
 * Copyright (c) YugaByte, Inc.
 */

package util

import "github.com/spf13/cobra"

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
