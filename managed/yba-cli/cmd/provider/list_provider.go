/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var listProviderCmd = &cobra.Command{
	Use:     "list",
	GroupID: "action",
	Short:   "List YugabyteDB Anywhere providers",
	Long:    "List YugabyteDB Anywhere providers",
	Run: func(cmd *cobra.Command, args []string) {
		providerCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		providerutil.ListProviderUtil(cmd, "", providerCode)

	},
}

func init() {
	listProviderCmd.Flags().SortFlags = false

	listProviderCmd.Flags().StringP("name", "n", "", "[Optional] Name of the provider.")
	listProviderCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the provider, defaults to list all providers. "+
			"Allowed values: aws, gcp, azu, onprem, kubernetes.")
}
