/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ear

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/aws"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/azu"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/ciphertrust"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/gcp"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/hashicorp"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// EARCmd set of commands are used to perform operations on ears
// in YugabyteDB Anywhere
var EARCmd = &cobra.Command{
	Use:     "ear",
	Aliases: []string{"encryption-at-rest", "kms"},
	Short:   "Manage YugabyteDB Anywhere Encryption at Rest Configurations",
	Long:    "Manage YugabyteDB Anywhere Encryption at Rest Configurations",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	EARCmd.AddCommand(listEARCmd)
	EARCmd.AddCommand(describeEARCmd)
	EARCmd.AddCommand(deleteEARCmd)
	EARCmd.AddCommand(refreshEARCmd)
	EARCmd.AddCommand(aws.AWSEARCmd)
	EARCmd.AddCommand(azu.AzureEARCmd)
	EARCmd.AddCommand(gcp.GCPEARCmd)
	EARCmd.AddCommand(hashicorp.HashicorpVaultEARCmd)

	util.PreviewCommand(EARCmd, []*cobra.Command{ciphertrust.CipherTrustEARCmd})

	EARCmd.AddGroup(&cobra.Group{
		ID:    "action",
		Title: "Action Commands",
	})
	EARCmd.AddGroup(&cobra.Group{
		ID:    "type",
		Title: "Encryption At Rest Type Commands",
	})
}
