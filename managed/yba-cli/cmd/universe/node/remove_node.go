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

// removeNodeCmd represents the universe command
var removeNodeCmd = &cobra.Command{
	Use:   "remove",
	Short: "Remove a node instance to YugabyteDB Anywhere universe",
	Long: "Remove a node instance from YugabyteDB Anywhere universe and move its data out.\n" +
		"The same instance is not expected to be used for this cluster again.",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(universeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to remove node instance"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(nodeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to remove"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Remove", util.RemoveNode)

	},
}

func init() {
	removeNodeCmd.Flags().SortFlags = false
}
