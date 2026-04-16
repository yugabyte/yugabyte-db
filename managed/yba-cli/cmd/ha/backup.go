/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ha

import (
	"encoding/json"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var listBackupsCmd = &cobra.Command{
	Use:     "list-backup",
	Aliases: []string{"ls"},
	Short:   "List HA backups",
	Long:    "List available backups for HA configuration",
	Example: `yba ha list-backup --uuid <config-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No config UUID found to list backups\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		backups, response, err := authAPI.ListHABackups(configUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Backup", "List")
		}

		if backups == nil || len(backups) == 0 {
			logrus.Info("No backups found\n")
			return
		}

		if viper.GetString("output") == "json" {
			output, err := json.Marshal(backups)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			os.Stdout.Write(output)
			os.Stdout.Write([]byte("\n"))
		} else {
			logrus.Infof("Backups for config %s:\n",
				formatter.Colorize(configUUID, formatter.GreenColor))
			for _, backup := range backups {
				logrus.Infof("  - %s\n", backup)
			}
		}
	},
}

func init() {
	listBackupsCmd.Flags().SortFlags = false
	listBackupsCmd.Flags().String("uuid", "",
		"[Required] The UUID of the HA configuration")
	listBackupsCmd.MarkFlagRequired("uuid")
}
