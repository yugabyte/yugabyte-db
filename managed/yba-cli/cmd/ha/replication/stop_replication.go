/*
 * Copyright (c) YugabyteDB, Inc.
 */

package replication

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var stopReplicationCmd = &cobra.Command{
	Use:     "stop",
	Aliases: []string{"disable"},
	Short:   "Stop periodic backup replication",
	Long:    "Stop periodic backup replication for HA configuration",
	Example: `yba ha replication stop --uuid <config-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No config UUID found to stop replication\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		response, err := authAPI.StopPeriodicBackup(configUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Replication", "Stop")
		}

		logrus.Infof("Replication schedule stopped for config %s\n",
			formatter.Colorize(configUUID, formatter.GreenColor))
	},
}

func init() {
	stopReplicationCmd.Flags().SortFlags = false
	stopReplicationCmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the HA configuration")
	stopReplicationCmd.MarkFlagRequired("uuid")
}
