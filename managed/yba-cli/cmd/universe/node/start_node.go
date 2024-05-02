/*
 * Copyright (c) YugaByte, Inc.
 */

package node

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// startNodeCmd represents the universe command
var startNodeCmd = &cobra.Command{
	Use:   "start",
	Short: "Start a node instance in YugabyteDB Anywhere universe",
	Long: "Start a node instance in YugabyteDB Anywhere universe.\n" +
		"Start the server processes on a previously stopped node.\n" +
		"Ideally it is added back very soon.",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(universeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to start node instance"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(nodeName)) == 0 {
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
