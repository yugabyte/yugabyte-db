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

// stopNodeCmd represents the universe command
var stopNodeCmd = &cobra.Command{
	Use:   "stop",
	Short: "Stop a node instance in YugabyteDB Anywhere universe",
	Long: "Stop a node instance in YugabyteDB Anywhere universe.\n" +
		"Stop the server processes running on the node.",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(universeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to stop node instance"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(nodeName)) == 0 {
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
