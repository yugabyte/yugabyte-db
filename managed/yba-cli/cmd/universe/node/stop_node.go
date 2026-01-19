/*
 * Copyright (c) YugabyteDB, Inc.
 */

package node

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// stopNodeCmd represents the universe command
var stopNodeCmd = &cobra.Command{
	Use:     "stop-processes",
	Aliases: []string{"stop"},
	Short:   "Stop YugbayteDB processes on a node in YugabyteDB Anywhere universe",
	Long: "Stop YugbayteDB processes on a node in YugabyteDB Anywhere universe.\n" +
		"Stop the server processes running on the node.",
	Example: `yba universe node stop --name <universe-name> --node-name <node-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to stop node"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if util.IsEmptyString(nodeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to stop"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Stop", util.StopNode)

	},
}

func init() {
	stopNodeCmd.Flags().SortFlags = false
}
