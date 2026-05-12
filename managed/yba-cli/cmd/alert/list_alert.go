/*
* Copyright (c) YugabyteDB, Inc.
 */

package alert

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert"
)

// listAlertCmd represents the list alert command
var listAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere alerts",
	Long:    "List alerts in YugabyteDB Anywhere",
	Example: `yba alert list --source-name <source-name>`,
	Run: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
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

		sort, err := cmd.Flags().GetString("sorting-field")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if strings.Compare(sort, "create-time") == 0 {
			sort = "createTime"
		} else if strings.Compare(sort, "source-name") == 0 {
			sort = "sourceName"
		}

		alertCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  alert.NewAlertFormat(viper.GetString("output")),
		}

		var limit int32 = 10
		var offset int32 = 0

		alertAPIDirection, err := cmd.Flags().GetString("direction")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		alertAPIQuery := ybaclient.AlertPagedApiQuery{
			Filter:    alertAPIFilter,
			Direction: strings.ToUpper(alertAPIDirection),
			Limit:     limit,
			Offset:    offset,
			SortBy:    sort,
		}

		alertListRequest := authAPI.PageAlerts().PageAlertsRequest(alertAPIQuery)
		alerts := make([]ybaclient.Alert, 0)
		force := viper.GetBool("force")
		for {
			// Execute alert list request
			r, response, err := alertListRequest.Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Alert", "List")
			}

			// Check if alerts found
			if len(r.GetEntities()) < 1 {
				if util.IsOutputType(formatter.TableFormatKey) {
					logrus.Info("No alerts found\n")
				} else {
					logrus.Info("[]\n")
				}
				return
			}

			// Write alert entities
			if force {
				alerts = append(alerts, r.GetEntities()...)
			} else {
				alert.Write(alertCtx, r.GetEntities())
			}

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				if util.IsOutputType(formatter.TableFormatKey) && !force {
					logrus.Info("No more alerts present\n")
				}
				break
			}

			err = util.ConfirmCommand(
				"List more entries",
				viper.GetBool("force"))
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			alertAPIQuery.Offset = offset
			alertListRequest = authAPI.PageAlerts().PageAlertsRequest(alertAPIQuery)
		}
		if force {
			alert.Write(alertCtx, alerts)
		}
	},
}

func init() {
	listAlertCmd.Flags().SortFlags = false

	listAlertCmd.Flags().String("configuration-uuid", "",
		"[Optional] Configuration UUID to filter alerts.")
	listAlertCmd.Flags().String("configuration-types", "",
		"[Optional] Comma separated list of configuration types.")

	listAlertCmd.Flags().String("severities", "",
		"[Optional] Comma separated list of severities. Allowed values: severe, warning.")

	listAlertCmd.Flags().String("source-uuids", "",
		"[Optional] Comma separated list of source UUIDs.")

	listAlertCmd.Flags().String("source-name", "",
		"[Optional] Source name to filter alerts.")

	listAlertCmd.Flags().String("states", "",
		"[Optional] Comma separated list of states. "+
			"Allowed values: active, acknowledged, suspended, resolved.")

	listAlertCmd.Flags().String("uuids", "",
		"[Optional] Comma separated list of alert UUIDs.")

	listAlertCmd.Flags().String("sorting-field", "create-time",
		"[Optional] Field to sort alerts. "+
			"Allowed values: severity, create-time, source-name, name, state, uuid.")

	listAlertCmd.Flags().String("direction", "desc",
		"[Optional] Direction to sort alerts. "+
			"Allowed values: asc, desc.")

	listAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
