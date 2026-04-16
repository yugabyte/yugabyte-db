/*
 * Copyright (c) YugabyteDB, Inc.
 */

package provider

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteProviderCmd represents the provider command
var deleteProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	GroupID: "action",
	Short:   "Delete a YugabyteDB Anywhere provider",
	Long:    "Delete a provider in YugabyteDB Anywhere",
	Example: `yba provider delete --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {

		providerCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		providerutil.DeleteProviderUtil(cmd, "", strings.ToLower(providerCode))
	},
}

func init() {
	deleteProviderCmd.Flags().SortFlags = false
	deleteProviderCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the provider to be deleted.")
	deleteProviderCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the provider. "+
			"Allowed values: aws, gcp, azu, onprem, kubernetes.")
	deleteProviderCmd.MarkFlagRequired("name")
	deleteProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
