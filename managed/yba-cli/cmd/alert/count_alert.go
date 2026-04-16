/*
* Copyright (c) YugabyteDB, Inc.
 */

package alert

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// countAlertCmd represents the count alert command
var countAlertCmd = &cobra.Command{
	Use:     "count",
	Short:   "Count YugabyteDB Anywhere alerts",
	Long:    "Count alerts in YugabyteDB Anywhere",
	Example: `yba alert count`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		alertAPIFilter := ybaclient.AlertApiFilter{}

		sourceUUIDs, err := cmd.Flags().GetString("source-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(sourceUUIDs) {
			alertAPIFilter.SetSourceUUIDs(strings.Split(sourceUUIDs, ","))
		}

		sourceName, err := cmd.Flags().GetString("source-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(sourceName) {
			alertAPIFilter.SetSourceName(sourceName)
		}

		configurationUUID, err := cmd.Flags().GetString("configuration-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(configurationUUID) {
			alertAPIFilter.SetConfigurationUuid(configurationUUID)
		}

		configurationTypes, err := cmd.Flags().GetString("configuration-types")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(configurationTypes) {
			alertAPIFilter.SetConfigurationTypes(strings.Split(configurationTypes, ","))
		}

		severities, err := cmd.Flags().GetString("severities")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(severities) {
			severitiesList := strings.Split(severities, ",")
			if len(severitiesList) > 0 {
				for i, severity := range severitiesList {
					severitiesList[i] = strings.ToUpper(severity)
				}
			}
			alertAPIFilter.SetSeverities(severitiesList)
		}

		states, err := cmd.Flags().GetString("states")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(states) {
			statesList := strings.Split(states, ",")
			if len(statesList) > 0 {
				for i, state := range statesList {
					statesList[i] = strings.ToUpper(state)
				}
			}
			alertAPIFilter.SetStates(statesList)
		}

		uuids, err := cmd.Flags().GetString("uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(uuids) {
			alertAPIFilter.SetUuids(strings.Split(uuids, ","))
		}

		alertCountRequest := authAPI.CountAlerts().CountAlertsRequest(alertAPIFilter)
		r, response, err := alertCountRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert", "Count")
		}

		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Infof("Number of alerts: %d\n", r)
		} else {
			logrus.Infof("%d\n", r)
		}

	},
}

func init() {
	countAlertCmd.Flags().SortFlags = false

	countAlertCmd.Flags().String("configuration-uuid", "",
		"[Optional] Configuration UUID to filter alerts.")
	countAlertCmd.Flags().String("configuration-types", "",
		"[Optional] Comma separated list of configuration types.")

	countAlertCmd.Flags().String("severities", "",
		"[Optional] Comma separated list of severities. Allowed values: severe, warning.")

	countAlertCmd.Flags().String("source-uuids", "",
		"[Optional] Comma separated list of source UUIDs.")

	countAlertCmd.Flags().String("source-name", "",
		"[Optional] Source name to filter alerts.")

	countAlertCmd.Flags().String("states", "",
		"[Optional] Comma separated list of states. "+
			"Allowed values: active, acknowledged, suspended, resolved.")

	countAlertCmd.Flags().String("uuids", "",
		"[Optional] Comma separated list of alert UUIDs.")

}
