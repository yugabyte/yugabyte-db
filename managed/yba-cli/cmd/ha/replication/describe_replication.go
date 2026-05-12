/*
 * Copyright (c) YugabyteDB, Inc.
 */

package replication

import (
	"encoding/json"
	"io"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var describeReplicationCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Get replication schedule info",
	Long:    "Get replication schedule information for HA configuration",
	Example: `yba ha replication describe --uuid <config-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No config UUID found to get replication info\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		response, err := authAPI.GetBackupInfo(configUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Replication", "Describe")
		}
		defer response.Body.Close()
		body, err := io.ReadAll(response.Body)
		if err != nil {
			logrus.Fatalf(
				formatter.Colorize("Error reading response: "+err.Error()+"\n", formatter.RedColor),
			)
		}

		// Parse JSON to check validity and for pretty printing
		var backupInfo map[string]interface{}
		if err := json.Unmarshal(body, &backupInfo); err != nil {
			logrus.Fatalf(
				formatter.Colorize("Error parsing response: "+err.Error()+"\n", formatter.RedColor),
			)
		}

		if viper.GetString("output") == "json" {
			os.Stdout.Write(body)
			os.Stdout.Write([]byte("\n"))
		} else {
			logrus.Infof("Replication Schedule Info for config %s:\n",
				formatter.Colorize(configUUID, formatter.GreenColor))
			prettyJSON, err := json.MarshalIndent(backupInfo, "  ", "  ")
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize("Error formatting response: "+err.Error()+"\n", formatter.RedColor),
				)
			}
			logrus.Infof("  %s\n", string(prettyJSON))
		}
	},
}

func init() {
	describeReplicationCmd.Flags().SortFlags = false
	describeReplicationCmd.Flags().String("uuid", "",
		"[Required] The UUID of the HA configuration")
	describeReplicationCmd.MarkFlagRequired("uuid")
}
