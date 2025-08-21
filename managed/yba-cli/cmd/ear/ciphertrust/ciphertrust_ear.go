/*
 * Copyright (c) YugaByte, Inc.
 */

package ciphertrust

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// CipherTrustEARCmd represents the ear command
var CipherTrustEARCmd = &cobra.Command{
	Use:     "ciphertrust",
	Aliases: []string{"ct"},
	GroupID: "type",
	Short:   "Manage a YugabyteDB Anywhere CipherTrust encryption at rest (EAR) configuration",
	Long:    "Manage a CipherTrust encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	CipherTrustEARCmd.Flags().SortFlags = false

	CipherTrustEARCmd.AddCommand(createCipherTrustEARCmd)
	CipherTrustEARCmd.AddCommand(updateCipherTrustEARCmd)
	CipherTrustEARCmd.AddCommand(listCipherTrustEARCmd)
	CipherTrustEARCmd.AddCommand(describeCipherTrustEARCmd)
	CipherTrustEARCmd.AddCommand(deleteCipherTrustEARCmd)
	CipherTrustEARCmd.AddCommand(refreshCipherTrustEARCmd)

	CipherTrustEARCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update and refresh.",
				formatter.GreenColor)))
}
