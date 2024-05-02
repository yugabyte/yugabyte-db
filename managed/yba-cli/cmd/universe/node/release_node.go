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

// releaseNodeCmd represents the universe command
var releaseNodeCmd = &cobra.Command{
	Use:   "release",
	Short: "Release a node instance from YugabyteDB Anywhere universe",
	Long: "Release a node instance from YugabyteDB Anywhere universe.\n" +
		"Release the instance to the IaaS/provider. Only for stopped/removed nodes.",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(universeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to release node instance"+
					"\n", formatter.RedColor))
		}
		nodeName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(nodeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to release"+
					"\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		nodeOperationsUtil(cmd, "Release", util.ReleaseNode)

	},
}

func init() {
	releaseNodeCmd.Flags().SortFlags = false
}
