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

// decommissionNodeCmd represents the universe command
var decommissionNodeCmd = &cobra.Command{
	Use:   "decommission",
	Short: "Decommission a node in YugabyteDB Anywhere universe",
	Long: "Decommission a node in YugabyteDB Anywhere universe. " +
		"Performs scaling-in of the universe by removing (detaching) the node permanently from the universe.",
	Example: `yba universe node decommission --name <universe-name> --node-name <node-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to decommission node"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if util.IsEmptyString(nodeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to decommission"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Decommission", util.DecommissionNode)

	},
}

func init() {
	decommissionNodeCmd.Flags().SortFlags = false
}
