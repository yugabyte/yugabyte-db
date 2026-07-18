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

// startNodeCmd represents the universe command
var startNodeCmd = &cobra.Command{
	Use:     "start-processes",
	Aliases: []string{"start"},
	Short:   "Start YugbayteDB processes on a node in YugabyteDB Anywhere universe",
	Long: "Start YugbayteDB processes on a node in YugabyteDB Anywhere universe." +
		"Start the server processes on a previously stopped node." +
		"Ideally it is added back very soon.",
	Example: `yba universe node start --name <universe-name> --node-name <node-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to start node"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if util.IsEmptyString(nodeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to start"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Start", util.StartNode)

	},
}

func init() {
	startNodeCmd.Flags().SortFlags = false
}
