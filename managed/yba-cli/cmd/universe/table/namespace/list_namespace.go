/*
 * Copyright (c) YugabyteDB, Inc.
 */

package namespace

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/table"
)

var listNamespaceCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere universe table namespaces",
	Long:    "List YugabyteDB Anywhere universe table namespaces",
	Example: `yba universe table namespace list --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to list table namespaces"+
					"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeListRequest := authAPI.ListUniverses()

		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeListRequest = universeListRequest.Name(universeName)

		universeList, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Namespace", "List - Fetch Universe")
		}

		if len(universeList) < 1 {
			logrus.Fatalf("No universes with name: %s found\n", universeName)
		}
		selectedUniverse := universeList[0]
		universeUUID := selectedUniverse.GetUniverseUUID()

		includeSystemNamespaceInfo, err := cmd.Flags().GetBool("include-system-namespaces")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r, response, err := authAPI.
			GetAllNamespaces(
				universeUUID,
			).IncludeSystemNamespaces(includeSystemNamespaceInfo).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Namespace", "List")
		}

		namespaceName, err := cmd.Flags().GetString("namespace-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		namespaces := make([]ybaclient.NamespaceInfoResp, 0)
		if util.IsEmptyString(namespaceName) {
			namespaces = r
		} else {
			for _, namespace := range r {
				if strings.Compare(namespace.GetName(), namespaceName) == 0 {
					namespaces = append(namespaces, namespace)
				}
			}
		}

		namespaceCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  table.NewNamespaceFormat(viper.GetString("output")),
		}
		if len(namespaces) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No universe namespace found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		table.NamespaceWrite(namespaceCtx, namespaces)

	},
}

func init() {
	listNamespaceCmd.Flags().SortFlags = false

	listNamespaceCmd.Flags().String("namespace-name", "",
		"[Optional] Namespace name to be listed.")
	listNamespaceCmd.Flags().Bool("include-system-namespaces", false,
		"[Optional] Include system namespaces. (default false)")
}
