/*
 * Copyright (c) YugabyteDB, Inc.
 */

package pitr

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var deletePITRCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete the PITR configuration for the universe",
	Long:    `Delete the Point-In-Time Recovery (PITR) configuration for the universe`,
	Example: `yba backup pitr delete --universe-name <universe-name> --uuid <pitr-uuid>`,
	Run: func(cmd *cobra.Command, args []string) {
		universeName := util.MustGetFlagString(cmd, "universe-name")
		pitrUUID := util.MustGetFlagString(cmd, "uuid")
		DeletePITRUtil(cmd, universeName, pitrUUID)
	},
}

func init() {
	deletePITRCmd.Flags().SortFlags = false
	deletePITRCmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the PITR config to be deleted.")
	deletePITRCmd.MarkFlagRequired("uuid")
}
