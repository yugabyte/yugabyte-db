/*
 * Copyright (c) YugabyteDB, Inc.
 */

package security

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/security/eit"
)

// SecurityUniverseCmd represents the universe security command
var SecurityUniverseCmd = &cobra.Command{
	Use:     "security",
	GroupID: "action",
	Short:   "Manage security settings for a universe",
	Long:    "Manage security settings for a universe",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	SecurityUniverseCmd.Flags().SortFlags = false
	SecurityUniverseCmd.PersistentFlags().SortFlags = false

	SecurityUniverseCmd.AddCommand(encryptionAtRestCmd)
	SecurityUniverseCmd.AddCommand(eit.EncryptionInTransitCmd)

	SecurityUniverseCmd.PersistentFlags().StringP("name", "n", "",
		"[Required] The name of the universe for the operation.")
	SecurityUniverseCmd.MarkPersistentFlagRequired("name")
	SecurityUniverseCmd.PersistentFlags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	SecurityUniverseCmd.PersistentFlags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")

}
