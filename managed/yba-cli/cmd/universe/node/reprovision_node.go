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

// reprovisionNodeCmd represents the universe command
var reprovisionNodeCmd = &cobra.Command{
	Use:   "reprovision",
	Short: "Reprovision a node instance in YugabyteDB Anywhere universe",
	Long: "Reprovision a node instance in YugabyteDB Anywhere universe.\n" +
		"Re-provision node with already stopped processes.",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(universeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to reprovision node instance"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(nodeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to reprovision"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Reprovision", util.ReprovisionNode)

	},
}

func init() {
	reprovisionNodeCmd.Flags().SortFlags = false
}
