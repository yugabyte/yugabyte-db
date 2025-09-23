/*
 * Copyright (c) YugabyteDB, Inc.
 */

package pitr

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// RecoverPITRCmd set of commands are used to perform recover operations on universes
// in YugabyteDB Anywhere
var RecoverPITRCmd = &cobra.Command{
	Use:     "recover",
	Aliases: []string{"restore"},
	Short:   "Recover universe keyspace to a point in time",
	Long:    "Recover universe keyspace to a point in time",
	Example: `yba backup pitr recover --universe-name <universe-name> --uuid <pitr-uuid>
	 --timestamp <timestamp>`,
	Run: func(cmd *cobra.Command, args []string) {
		universeName := util.MustGetFlagString(cmd, "universe-name")
		pitrUUID := util.MustGetFlagString(cmd, "uuid")
		timestamp := util.MustGetFlagInt64(cmd, "timestamp")
		RecoverToPointInTimeUtil(cmd, universeName, pitrUUID, timestamp)
	},
}

func init() {
	RecoverPITRCmd.Flags().SortFlags = false
	RecoverPITRCmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the PITR config.")
	RecoverPITRCmd.MarkFlagRequired("uuid")
	RecoverPITRCmd.Flags().Int64P("timestamp", "t", 0,
		"[Required] Unix epoch timestamp in milliseconds to which the "+
			"universe keyspace needs to be recovered.")
	RecoverPITRCmd.MarkFlagRequired("timestamp")
}
