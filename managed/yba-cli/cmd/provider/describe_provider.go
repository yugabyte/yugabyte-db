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

var describeProviderCmd = &cobra.Command{
	Use:     "describe",
	GroupID: "action",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere provider",
	Long:    "Describe a provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		providerutil.DescribeProviderUtil(cmd, "", providerCode)

	},
}

func init() {
	describeProviderCmd.Flags().SortFlags = false
	describeProviderCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the provider to get details.")
	describeProviderCmd.MarkFlagRequired("name")
	describeProviderCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the provider. "+
			"Allowed values: aws, gcp, azu, onprem, kubernetes.")
}
