/*
 * Copyright (c) YugabyteDB, Inc.
 */

package scope

import (
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"

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

		populateCustomerProviderAndUniverses(authAPI)

		r, response, err := authAPI.ListScopes().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Runtime Configuration Scope", "List")
		}

		scopeType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scopeType) > 0 {
			rList := make([]ybaclient.ScopedConfig, 0)
			for _, keyInfo := range r.GetScopedConfigList() {
				if strings.EqualFold(keyInfo.GetType(), scopeType) {
					rList = append(rList, keyInfo)
				}
			}
			r.SetScopedConfigList(rList)
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

func init() {
	listScopeCmd.Flags().SortFlags = false
	listScopeCmd.Flags().
		String("type", "", "[Optional] Scope type. Allowed values: universe, customer, provider, global.")
}

func populateCustomerProviderAndUniverses(authAPI *ybaAuthClient.AuthAPIClient) {
	var err error
	var response *http.Response
	scope.Customers, response, err = authAPI.ListOfCustomers().Execute()
	if err != nil {
		util.FatalHTTPError(
			response,
			err,
			"Runtime Configuration Scope",
			"List - Get List of Customers",
		)
	}
	scope.Providers, response, err = authAPI.GetListOfProviders().Execute()
	if err != nil {
		util.FatalHTTPError(
			response,
			err,
			"Runtime Configuration Scope",
			"List - Get List of Providers",
		)
	}

	scope.Universes, response, err = authAPI.ListUniverses().Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Runtime Configuration Scope", "List - List Universes")
	}
}
