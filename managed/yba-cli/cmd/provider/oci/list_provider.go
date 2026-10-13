/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listOCIProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List OCI YugabyteDB Anywhere providers",
	Long:    "List OCI YugabyteDB Anywhere providers",
	Example: `yba provider oci list`,
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "OCI", util.OCIProviderType)
	},
}

func init() {
	listOCIProviderCmd.Flags().SortFlags = false
}
