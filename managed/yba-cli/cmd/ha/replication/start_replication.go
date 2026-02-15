/*
 * Copyright (c) YugabyteDB, Inc.
 */

package replication

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var startReplicationCmd = &cobra.Command{
	Use:     "start",
	Aliases: []string{"enable"},
	Short:   "Start periodic backup replication",
	Long:    "Start periodic backup replication for HA configuration",
	Example: `yba ha replication start --uuid <config-uuid> --frequency-ms 60000`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No config UUID found to start replication\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		frequencyMs, err := cmd.Flags().GetInt64("frequency-ms")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		formData := ybaclient.PlatformBackupFrequencyFormData{
			FrequencyMilliseconds: frequencyMs,
		}
		response, err := authAPI.StartPeriodicBackup(configUUID).
			PlatformBackupFrequencyRequest(formData).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Replication", "Start")
		}

		logrus.Infof("Replication schedule started with frequency %d ms for config %s\n",
			frequencyMs, formatter.Colorize(configUUID, formatter.GreenColor))
	},
}

func init() {
	startReplicationCmd.Flags().SortFlags = false
	startReplicationCmd.Flags().String("uuid", "",
		"[Required] The UUID of the HA configuration")
	startReplicationCmd.MarkFlagRequired("uuid")
	startReplicationCmd.Flags().Int64P("frequency-ms", "f", 60000,
		"[Optional] Backup frequency in milliseconds (default 60000 = 1 minute)")
}
