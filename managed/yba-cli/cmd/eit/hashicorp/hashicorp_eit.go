/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// HashicorpVaultEITCmd represents the eit command
var HashicorpVaultEITCmd = &cobra.Command{
	Use:     "hashicorp",
	Aliases: []string{"hashicorp-vault", "hcv"},
	GroupID: "type",
	Short: "Manage a YugabyteDB Anywhere Hashicorp Vault encryption " +
		"in transit (EIT) certificate configuration",
	Long: "Manage a Hashicorp Vault encryption in transit (EIT) " +
		"certificate configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	HashicorpVaultEITCmd.Flags().SortFlags = false

	// HashicorpVaultEITCmd.AddCommand(createHashicorpVaultEITCmd)
	// HashicorpVaultEITCmd.AddCommand(updateHashicorpVaultEITCmd)
	HashicorpVaultEITCmd.AddCommand(listHashicorpVaultEITCmd)
	HashicorpVaultEITCmd.AddCommand(describeHashicorpVaultEITCmd)
	HashicorpVaultEITCmd.AddCommand(deleteHashicorpVaultEITCmd)

	HashicorpVaultEITCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}
