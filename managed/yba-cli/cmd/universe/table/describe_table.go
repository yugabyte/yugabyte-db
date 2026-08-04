/*
 * Copyright (c) YugabyteDB, Inc.
 */

package table

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/table"
)

var describeTableCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere universe table",
	Long:    "Describe a universe table in YugabyteDB Anywhere",
	Example: `yba universe table describe --name <universe-name> --table-uuid <table-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to describe\n", formatter.RedColor))
		}

		tableUUIDFlag, err := cmd.Flags().GetString("table-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(tableUUIDFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No table uuid found to describe\n", formatter.RedColor))
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

		rUniverse, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Table", "Describe - Fetch Universe")
		}

		if len(rUniverse) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", universeName),
					formatter.RedColor,
				))
		}

		universeUUID := rUniverse[0].GetUniverseUUID()

		tableUUID, err := cmd.Flags().GetString("table-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rTable, response, err := authAPI.DescribeTable(universeUUID, tableUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Table", "Describe")
		}

		r := util.CheckAndAppend(
			make([]ybaclient.TableDefinitionTaskParams, 0),
			rTable,
			fmt.Sprintf("Table %s for universe %s (%s) not found",
				tableUUID,
				universeName,
				universeUUID,
			),
		)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullTableContext := *table.NewFullDefinitionContext()
			fullTableContext.Output = os.Stdout
			fullTableContext.Format = table.NewFullDefinitionFormat(viper.GetString("output"))
			fullTableContext.SetFullDefinition(r[0])
			fullTableContext.Write()
			return
		}

		tableCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  table.NewTableFormat(viper.GetString("output")),
		}
		table.DefinitionWrite(tableCtx, r)

	},
}

func init() {
	describeTableCmd.Flags().SortFlags = false
	describeTableCmd.Flags().String("table-uuid", "",
		"[Required] The UUID of the table to be described.")
	describeTableCmd.MarkFlagRequired("table-uuid")
}
