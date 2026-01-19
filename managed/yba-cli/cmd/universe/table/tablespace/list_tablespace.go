/*
 * Copyright (c) YugabyteDB, Inc.
 */

package tablespace

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

var listTablespaceCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere universe tablespaces",
	Long:    "List YugabyteDB Anywhere universe tablespaces",
	Example: `yba universe table tablespace list --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to list tablespaces"+
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
			util.FatalHTTPError(response, err, "Tablespace", "List - Fetch Universe")
		}

		if len(universeList) < 1 {
			logrus.Fatalf("No universes with name: %s found\n", universeName)
		}
		selectedUniverse := universeList[0]
		universeUUID := selectedUniverse.GetUniverseUUID()

		r, response, err := authAPI.GetAllTableSpaces(universeUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Tablespace", "List")
		}

		tablespaceName, err := cmd.Flags().GetString("tablespace-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tablespaces := make([]ybaclient.TableSpaceInfo, 0)
		if util.IsEmptyString(tablespaceName) {
			tablespaces = r
		} else {
			for _, tablespace := range r {
				if strings.Compare(tablespace.GetName(), tablespaceName) == 0 {
					tablespaces = append(tablespaces, tablespace)
				}
			}
		}

		tablespaceCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  table.NewTablespaceFormat(viper.GetString("output")),
		}
		if len(tablespaces) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No tablespace found in universe\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		table.TablespaceWrite(tablespaceCtx, tablespaces)

	},
}

func init() {
	listTablespaceCmd.Flags().SortFlags = false

	listTablespaceCmd.Flags().String("tablespace-name", "",
		"[Optional] Tablespace name to be listed.")
}
