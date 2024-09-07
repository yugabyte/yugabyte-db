/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// HashicorpVaultEARCmd represents the ear command
var HashicorpVaultEARCmd = &cobra.Command{
	Use:     "hashicorp",
	Aliases: []string{"hashicorp-vault", "hcv"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration",
	Long:    "Manage a Hashicorp Vault encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	HashicorpVaultEARCmd.Flags().SortFlags = false

	HashicorpVaultEARCmd.AddCommand(createHashicorpVaultEARCmd)
	HashicorpVaultEARCmd.AddCommand(updateHashicorpVaultEARCmd)
	HashicorpVaultEARCmd.AddCommand(listHashicorpVaultEARCmd)
	HashicorpVaultEARCmd.AddCommand(describeHashicorpVaultEARCmd)
	HashicorpVaultEARCmd.AddCommand(deleteHashicorpVaultEARCmd)

	HashicorpVaultEARCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}
