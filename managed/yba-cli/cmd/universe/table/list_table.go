/*
 * Copyright (c) YugabyteDB, Inc.
 */

package table

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

var listTableCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere universe tables",
	Long:    "List YugabyteDB Anywhere universe tables",
	Example: `yba universe table list --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to list tables"+
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
			util.FatalHTTPError(response, err, "Table", "List - Fetch Universe")
		}

		if len(universeList) < 1 {
			logrus.Fatalf("No universes with name: %s found\n", universeName)
		}
		selectedUniverse := universeList[0]
		universeUUID := selectedUniverse.GetUniverseUUID()

		includeParentTableInfo, err := cmd.Flags().GetBool("include-parent-table-info")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		includeColocatedParentTables, err := cmd.Flags().GetBool("include-colocated-parent-tables")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		xclusterSupportedOnly, err := cmd.Flags().GetBool("xcluster-supported-only")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r, response, err := authAPI.
			GetAllTables(universeUUID).IncludeParentTableInfo(includeParentTableInfo).
			IncludeColocatedParentTables(includeColocatedParentTables).
			XClusterSupportedOnly(xclusterSupportedOnly).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Table", "List")
		}

		tableName, err := cmd.Flags().GetString("table-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tables := make([]ybaclient.TableInfoResp, 0)
		if util.IsEmptyString(tableName) {
			tables = r
		} else {
			for _, table := range r {
				if strings.Compare(table.GetTableName(), tableName) == 0 {
					tables = append(tables, table)
				}
			}
		}

		tableCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  table.NewTableFormat(viper.GetString("output")),
		}
		if len(tables) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No universe table found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		table.Write(tableCtx, tables)

	},
}

func init() {
	listTableCmd.Flags().SortFlags = false

	listTableCmd.Flags().String("table-name", "",
		"[Optional] Table name to be listed.")
	listTableCmd.Flags().Bool("include-parent-table-info", false,
		"[Optional] Include parent table information. (default false)")
	listTableCmd.Flags().Bool("include-colocated-parent-tables", true,
		"[Optional] Include colocated parent tables.")
	listTableCmd.Flags().Bool("xcluster-supported-only", false,
		"[Optional] Include only xCluster supported tables. (default false)")
}
