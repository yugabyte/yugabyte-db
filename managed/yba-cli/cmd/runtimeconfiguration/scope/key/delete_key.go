/*
 * Copyright (c) YugabyteDB, Inc.
 */

package key

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteKeyCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere runtime configuration scope key value",
	Long:    "Delete a runtime configuration scope key value in YugabyteDB Anywhere ",
	Example: `yba runtime-config scope key delete --uuid <scope> --name <key-name> --value <value>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		scopeNameFlag, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scopeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No scope uuid found to delete\n", formatter.RedColor))
		}

		keyNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(keyNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No key name found to delete\n", formatter.RedColor))

		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		scopeName := util.MustGetFlagString(cmd, "uuid")
		keyName := util.MustGetFlagString(cmd, "name")
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		DeleteKey(authAPI, scopeName, keyName, true /*logSuccess*/)
	},
}

func init() {
	deleteKeyCmd.Flags().SortFlags = false
	deleteKeyCmd.Flags().StringP("uuid", "u", "",
		"[Required] The scope UUID of the key to be deleted.")
	deleteKeyCmd.MarkFlagRequired("uuid")

	deleteKeyCmd.Flags().StringP("name", "n", "",
		"[Required] The key name to be deleted.")
	deleteKeyCmd.MarkFlagRequired("name")

}
