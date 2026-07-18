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

// replaceNodeCmd represents the universe command
var replaceNodeCmd = &cobra.Command{
	Use:     "replace",
	Short:   "Decommission and replace a node in a universe with another new node",
	Long:    "Decommission and replace a node in a universe with another new node.",
	Example: `yba universe node replace --name <universe-name> --node-name <node-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to replace node"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if util.IsEmptyString(nodeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to replace"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Replace", util.ReplaceNode)

	},
}

func init() {
	replaceNodeCmd.Flags().SortFlags = false
}
