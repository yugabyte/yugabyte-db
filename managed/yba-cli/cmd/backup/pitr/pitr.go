/*
* Copyright (c) YugabyteDB, Inc.
 */

package pitr

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// PITRCmd set of commands are used to perform pitr on universes
// in YugabyteDB Anywhere
var PITRCmd = &cobra.Command{
	Use:   "pitr",
	Short: "Manage YugabyteDB Anywhere universe PITR configs",
	Long:  "Manage YugabyteDB Anywhere universe PITR (Point In Time Recovery) configs",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	PITRCmd.Flags().SortFlags = false
	PITRCmd.AddCommand(createPITRCmd)
	PITRCmd.AddCommand(listPITRCmd)
	PITRCmd.AddCommand(deletePITRCmd)
	PITRCmd.AddCommand(RecoverPITRCmd)
	util.PreviewCommand(PITRCmd, []*cobra.Command{editPITRCmd})
	PITRCmd.PersistentFlags().String("universe-name", "",
		"[Required] The name of the universe associated with the PITR configuration.")
	PITRCmd.MarkPersistentFlagRequired("universe-name")
}
