/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instance

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var promoteHAInstanceCmd = &cobra.Command{
	Use:     "promote-instance",
	Aliases: []string{"promote"},
	Short:   "Promote the local standby to leader",
	Long:    "Promote the local platform instance from standby to leader using a backup from the current leader",
	Example: `yba ha instance promote-instance --uuid <uuid> --instance-uuid <instance-uuid>` +
		` --backup-file <filename> [--current-leader <instance-ip>] [--force]`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		instanceUUID, err := cmd.Flags().GetString("instance-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		backupFile, err := cmd.Flags().GetString("backup-file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"Config UUID is required to promote HA instance\n",
					formatter.RedColor,
				),
			)
		}
		if util.IsEmptyString(instanceUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"Instance UUID is required to promote HA instance\n",
					formatter.RedColor,
				),
			)
		}
		if util.IsEmptyString(backupFile) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"Backup file is required to promote HA instance\n",
					formatter.RedColor,
				),
			)
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		instanceUUID, err := cmd.Flags().GetString("instance-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		backupFile, err := cmd.Flags().GetString("backup-file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		curLeader, err := cmd.Flags().GetString("current-leader")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		isForcePromote, err := cmd.Flags().GetBool("force")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		formData := ybaclient.RestorePlatformBackupFormData{
			BackupFile: backupFile,
		}

		req := authAPI.PromoteHAInstance(configUUID, instanceUUID).
			PlatformBackupRestoreRequest(formData).IsForcePromote(isForcePromote)
		if !util.IsEmptyString(curLeader) {
			req = req.CurLeader(curLeader)
		}

		response, err := req.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Instance", "Promote")
		}

		logrus.Infof("HA instance %s has been promoted to leader\n",
			formatter.Colorize(instanceUUID, formatter.GreenColor))
	},
}

func init() {
	promoteHAInstanceCmd.Flags().SortFlags = false
	promoteHAInstanceCmd.Flags().String("uuid", "",
		"[Required] UUID of the HA configuration")
	promoteHAInstanceCmd.MarkFlagRequired("uuid")
	promoteHAInstanceCmd.Flags().String("instance-uuid", "",
		"[Required] UUID of the local instance to promote")
	promoteHAInstanceCmd.MarkFlagRequired("instance-uuid")
	promoteHAInstanceCmd.Flags().String("backup-file", "",
		"[Required] Backup file name from the current leader")
	promoteHAInstanceCmd.MarkFlagRequired("backup-file")
	promoteHAInstanceCmd.Flags().String("current-leader", "",
		"[Optional] IP address of instance (current leader) (default: resolved from config)")
	promoteHAInstanceCmd.Flags().BoolP("force", "f", true,
		"[Optional] Skip connection check to current leader (default: true)")

}
