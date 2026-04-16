/*
 * Copyright (c) YugabyteDB, Inc.
 */

package scope

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/scope"
)

var describeScopeCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere runtime configuration scope",
	Long: "Describe a runtime configuration scope in YugabyteDB Anywhere " +
		"and list all configurations under it.",
	Example: `yba runtime-config scope describe --uuid <scope>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		scopeNameFlag, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scopeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No scope uuid found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		populateCustomerProviderAndUniverses(authAPI)

		scopeName, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r, response, err := authAPI.GetConfig(scopeName).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Runtime Configuration Scope", "Describe")
		}

		scopeConfig := util.CheckAndDereference(
			r,
			fmt.Sprintf("No scope with uuid: %s found", scopeName),
		)

		scopes := []ybaclient.ScopedConfig{scopeConfig}

		if len(scopes) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullScopeContext := *scope.NewFullScopeContext()
			fullScopeContext.Output = os.Stdout
			fullScopeContext.Format = scope.NewFullScopeFormat(viper.GetString("output"))
			fullScopeContext.SetFullScope(scopes[0])
			fullScopeContext.Write()
			return
		}

		if len(scopes) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No scope with uuid: %s found\n", scopeName),
					formatter.RedColor,
				))
		}

		scopeCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  scope.NewScopeFormat(viper.GetString("output")),
		}
		scope.Write(scopeCtx, scopes)

	},
}

func init() {
	describeScopeCmd.Flags().SortFlags = false
	describeScopeCmd.Flags().StringP("uuid", "u", "",
		"[Required] The scope UUID to be described. "+
			"Run \"yba [customer|universe|provider] list\" to fetch UUID of the scope type.")
	describeScopeCmd.MarkFlagRequired("uuid")
}
