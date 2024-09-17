/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/node"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/security"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/upgrade"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// UniverseCmd set of commands are used to perform operations on universes
// in YugabyteDB Anywhere
var UniverseCmd = &cobra.Command{
	Use:   util.UniverseType,
	Short: "Manage YugabyteDB Anywhere universes",
	Long:  "Manage YugabyteDB Anywhere universes",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	UniverseCmd.AddCommand(listUniverseCmd)
	UniverseCmd.AddCommand(describeUniverseCmd)
	UniverseCmd.AddCommand(deleteUniverseCmd)
	UniverseCmd.AddCommand(createUniverseCmd)
	UniverseCmd.AddCommand(upgrade.UpgradeUniverseCmd)
	UniverseCmd.AddCommand(upgrade.RestartCmd)
	UniverseCmd.AddCommand(node.NodeCmd)
	UniverseCmd.AddCommand(security.SecurityUniverseCmd)
}
