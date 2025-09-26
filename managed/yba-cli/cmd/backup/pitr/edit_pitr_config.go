/*
 * Copyright (c) YugabyteDB, Inc.
 */

package pitr

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var editPITRCmd = &cobra.Command{
	Use:     "edit",
	Aliases: []string{"update"},
	Short:   "Edit the existing PITR configuration for the universe",
	Long:    "Edit Point-In-Time Recovery (PITR) configuration for the universe",
	Example: `yba backup pitr edit --universe-name <universe-name> --uuid <pitr-uuid> \
	--retention-in-secs <retention-in-secs>`,
	Run: func(cmd *cobra.Command, args []string) {
		universeName := util.MustGetFlagString(cmd, "universe-name")
		pitrUUID := util.MustGetFlagString(cmd, "uuid")
		retentionInSecs := util.MustGetFlagInt64(cmd, "retention-in-secs")
		EditPITRUtil(cmd, universeName, pitrUUID, retentionInSecs)
	},
}

func init() {
	editPITRCmd.Flags().SortFlags = false
	editPITRCmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the PITR config to be edited.")
	editPITRCmd.MarkFlagRequired("uuid")
	editPITRCmd.Flags().Int64P("retention-in-secs", "r", 0,
		"[Required] The updated retention period in seconds for the PITR config.")
	editPITRCmd.MarkFlagRequired("retention-in-secs")
}
