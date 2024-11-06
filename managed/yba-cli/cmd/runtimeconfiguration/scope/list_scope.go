/*
 * Copyright (c) YugaByte, Inc.
 */

package scope

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/scope"
)

var listScopeCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List runtime configuration scopes",
	Long:    "List runtime configuration scopes",
	Example: `yba runtime-config scope list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		r, response, err := authAPI.ListScopes().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response,
				err,
				"Runtime Configuration Scope", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		scopeCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  scope.NewScopeFormat(viper.GetString("output")),
		}
		if len(r.GetScopedConfigList()) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No scopes found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		scope.Write(scopeCtx, r.GetScopedConfigList())
	},
}
