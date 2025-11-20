/*
 * Copyright (c) YugabyteDB, Inc.
 */

package tablespace

import (
	"fmt"
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

var describeTablespaceCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere universe tablespace",
	Long:    "Describe a universe tablespace in YugabyteDB Anywhere",
	Example: `yba universe table tablespace describe --name <universe-name> \
	--tablespace-name <tablespace-name>`,
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

		tablespaceNameFlag, err := cmd.Flags().GetString("tablespace-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(tablespaceNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No tablespace name found to describe\n", formatter.RedColor))
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

		tablespaceName, err := cmd.Flags().GetString("tablespace-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rTablespace, response, err := authAPI.GetAllTableSpaces(universeUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Table", "Describe")
		}

		r := make([]ybaclient.TableSpaceInfo, 0)

		for _, table := range rTablespace {
			if strings.Compare(table.GetName(), tablespaceName) == 0 {
				r = append(r, table)
			}
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No tablespace with name: %s found\n", tablespaceName),
					formatter.RedColor,
				))
		}

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullTableContext := *table.NewFullTablespaceContext()
			fullTableContext.Output = os.Stdout
			fullTableContext.Format = table.NewFullTablespaceFormat(viper.GetString("output"))
			fullTableContext.SetFullTablespace(r[0])
			fullTableContext.Write()
			return
		}

		tablespaceCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  table.NewTableFormat(viper.GetString("output")),
		}
		table.TablespaceWrite(tablespaceCtx, r)

	},
}

func init() {
	describeTablespaceCmd.Flags().SortFlags = false
	describeTablespaceCmd.Flags().String("tablespace-name", "",
		"[Required] The Name of the tablespace to be described.")
	describeTablespaceCmd.MarkFlagRequired("tablespace-name")
}
