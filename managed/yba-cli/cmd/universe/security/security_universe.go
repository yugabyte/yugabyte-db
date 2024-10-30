/*
 * Copyright (c) YugaByte, Inc.
 */

package security

import "github.com/spf13/cobra"

// SecurityUniverseCmd represents the universe security command
var SecurityUniverseCmd = &cobra.Command{
	Use:   "security",
	Short: "Manage security settings for a universe",
	Long:  "Manage security settings for a universe",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	SecurityUniverseCmd.Flags().SortFlags = false

	SecurityUniverseCmd.AddCommand(encryptionAtRestCmd)

	SecurityUniverseCmd.PersistentFlags().StringP("name", "n", "",
		"[Required] The name of the universe for the operation.")
	SecurityUniverseCmd.MarkPersistentFlagRequired("name")
	SecurityUniverseCmd.PersistentFlags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	SecurityUniverseCmd.PersistentFlags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")

}
