/*
 * Copyright (c) YugabyteDB, Inc.
 */

package node

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
)

var listNodeCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere universe nodes",
	Long:    "List YugabyteDB Anywhere universe nodes",
	Example: `yba universe node list --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to list node"+
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

		r, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node", "List - Fetch Universe")
		}

		if len(r) < 1 {
			logrus.Fatalf("No universes with name: %s found\n", universeName)
		}
		selectedUniverse := r[0]
		details := selectedUniverse.GetUniverseDetails()
		nodes := details.GetNodeDetailsSet()

		NodeCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  universe.NewNodesFormat(viper.GetString("output")),
		}
		if len(nodes) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No universe nodes found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		universe.NodeWrite(NodeCtx, nodes)

	},
}

func init() {
	listNodeCmd.Flags().SortFlags = false
}
