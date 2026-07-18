/*
 * Copyright (c) YugabyteDB, Inc.
 */

package key

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/scope/key"
)

var getKeyCmd = &cobra.Command{
	Use:     "get",
	Aliases: []string{"fetch"},
	Short:   "Get a YugabyteDB Anywhere runtime configuration scope key value",
	Long: "Get a runtime configuration scope key value in YugabyteDB Anywhere. " +
		"Run \"yba runtime-config key-info list\" to get the list of keys in a scope type.",
	Example: `yba runtime-config scope key get --uuid <scope> --name <key-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		scopeNameFlag, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scopeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No scope uuid found to get\n", formatter.RedColor))
		}

		keyNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(keyNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No key name found to get\n", formatter.RedColor))

		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		scopeName, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		keyName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r, response, err := authAPI.GetConfigurationKey(scopeName, keyName).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Runtime Configuration Scope Key", "Get")
		}

		keys := []key.Key{}

		keys = append(keys, key.Key{
			Key:   keyName,
			Value: r,
		})

		keyCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  key.NewKeyFormat(viper.GetString("output")),
		}
		if len(keys) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Infof("No keys with name %s in scope %s found\n", keyName, scopeName)
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		key.Write(keyCtx, keys)

	},
}

func init() {
	getKeyCmd.Flags().SortFlags = false
	getKeyCmd.Flags().StringP("uuid", "u", "",
		"[Required] The scope UUID of the key to be fetched.")
	getKeyCmd.MarkFlagRequired("uuid")

	getKeyCmd.Flags().StringP("name", "n", "",
		"[Required] The key name to be fetched.")
	getKeyCmd.MarkFlagRequired("name")
}
