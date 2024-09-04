/*
 * Copyright (c) YugaByte, Inc.
 */

package eit

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/customca"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/hashicorp"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/k8scertmanager"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/selfsigned"
)

// EITCmd set of commands are used to perform operations on eit configs
// in YugabyteDB Anywhere
var EITCmd = &cobra.Command{
	Use:     "eit",
	Aliases: []string{"encryption-in-transit", "certs"},
	Short:   "Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations",
	Long:    "Manage YugabyteDB Anywhere Encryption In Transit (EIT) configurations",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	EITCmd.AddCommand(listEITCmd)
	EITCmd.AddCommand(describeEITCmd)
	EITCmd.AddCommand(deleteEITCmd)

	EITCmd.AddCommand(selfsigned.SelfSignedEITCmd)
	EITCmd.AddCommand(hashicorp.HashicorpVaultEITCmd)
	EITCmd.AddCommand(k8scertmanager.K8sCertManagerEITCmd)
	EITCmd.AddCommand(customca.CustomCAEITCmd)

	EITCmd.AddGroup(&cobra.Group{
		ID:    "action",
		Title: "Action Commands",
	})
	EITCmd.AddGroup(&cobra.Group{
		ID:    "type",
		Title: "EIT Type Commands",
	})
}
