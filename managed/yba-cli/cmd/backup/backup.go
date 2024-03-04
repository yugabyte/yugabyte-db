/*
* Copyright (c) YugaByte, Inc.
 */

package backup

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/backup/restore"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/backup/schedule"
)

// Set of backup commands used to perform operations on backups in YugabyteDB Anywhere
var BackupCmd = &cobra.Command{
	Use:   "backup",
	Short: "Manage YugabyteDB Anywhere backups",
	Long:  "Manage YugabyteDB Anywhere backups",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	BackupCmd.AddCommand(createBackupCmd)
	BackupCmd.AddCommand(listBackupCmd)
	BackupCmd.AddCommand(editBackupCmd)
	BackupCmd.AddCommand(deleteBackupCmd)
	BackupCmd.AddCommand(getBackupCmd)
	BackupCmd.AddCommand(listIncrementalBackupsCmd)
	BackupCmd.AddCommand(restore.RestoreCmd)
	BackupCmd.AddCommand(schedule.ScheduleCmd)
}
