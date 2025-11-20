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

// rebootNodeCmd represents the universe command
var rebootNodeCmd = &cobra.Command{
	Use:     "reboot",
	Short:   "Reboot a node in YugabyteDB Anywhere universe",
	Long:    "Reboot a node in YugabyteDB Anywhere universe.",
	Example: `yba universe node reboot --name <universe-name> --node-name <node-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to reboot node"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if util.IsEmptyString(nodeName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to reboot"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Reboot", util.RebootNode)

	},
}

func init() {
	rebootNodeCmd.Flags().SortFlags = false
}
