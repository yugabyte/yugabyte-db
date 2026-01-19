/*
* Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"fmt"
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/configuration"
)

// listConfigurationAlertCmd represents the list alert command
var listConfigurationAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere alert policies",
	Long:    "List alert policies in YugabyteDB Anywhere",
	Example: `yba alert policy list --source-name <source-name>`,
	Run: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		var response *http.Response
		var err error
		configuration.AlertDestinations, response, err = authAPI.ListAlertDestinations().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "List - Get Alert Destinations")
		}

		alertConfigurationAPIFilter := ybaclient.AlertConfigurationApiFilter{}
		alertConfigurationTarget := ybaclient.AlertConfigurationTarget{}

		targetUUIDs, err := cmd.Flags().GetString("target-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(targetUUIDs) {
			alertConfigurationTarget.SetUuids(strings.Split(targetUUIDs, ","))
			alertConfigurationTarget.SetAll(false)

		} else {
			alertConfigurationTarget.SetAll(true)
		}

		alertConfigurationAPIFilter.SetTarget(alertConfigurationTarget)

		targetType, err := cmd.Flags().GetString("target-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(targetType) {
			alertConfigurationAPIFilter.SetTargetType(strings.ToUpper(targetType))
			configuration.Templates, response, err = authAPI.ListAlertTemplates().
				ListTemplatesRequest(ybaclient.AlertTemplateApiFilter{
					TargetType: util.GetStringPointer(strings.ToUpper(targetType)),
				}).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Alert Policy", "List - Get Alert Templates")
			}
		}

		active, err := cmd.Flags().GetBool("active")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		alertConfigurationAPIFilter.SetActive(active)

		destinationType, err := cmd.Flags().GetString("destination-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(destinationType) {
			switch destinationType {
			case "no":
				destinationType = util.NoDestinationAlertConfigurationDestinationType
			case "default":
				destinationType = util.DefaultDestinationAlertConfigurationDestinationType
			case "selected":
				destinationType = util.SelectedDestinationAlertConfigurationDestinationType
			default:
				logrus.Fatalf(formatter.Colorize("Invalid destination type\n", formatter.RedColor))
			}
			alertConfigurationAPIFilter.SetDestinationType(destinationType)
		}

		destinationName, err := cmd.Flags().GetString("destination")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(destinationName) {
			rList, response, err := authAPI.ListAlertDestinations().Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Alert Policy", "List - List Alert Destinations")
			}
			destinationUUID := ""
			for _, r := range rList {
				if strings.Compare(r.GetName(), destinationName) == 0 {
					destinationUUID = r.GetUuid()
					break
				}
			}
			if len(destinationUUID) == 0 {
				logrus.Fatalf(
					formatter.Colorize(
						"No destination found with name: "+destinationName+"\n",
						formatter.RedColor))
			}
			alertConfigurationAPIFilter.SetDestinationUuid(destinationUUID)
		} else if destinationType == util.SelectedDestinationAlertConfigurationDestinationType {
			logrus.Fatalf(
				formatter.Colorize("Destination name is required for \"selected\" destination type.\n",
					formatter.RedColor))
		}

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(name) {
			alertConfigurationAPIFilter.SetName(name)
		}

		severity, err := cmd.Flags().GetString("severity")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(severity) {
			alertConfigurationAPIFilter.SetSeverity(strings.ToUpper(severity))
		}

		template, err := cmd.Flags().GetString("template")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(template) {
			alertConfigurationAPIFilter.SetTemplate(template)
		}

		uuids, err := cmd.Flags().GetString("uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(uuids) {
			alertConfigurationAPIFilter.SetUuids(strings.Split(uuids, ","))
		}

		sort, err := cmd.Flags().GetString("sorting-field")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		switch sort {
		case "uuid":
			sort = "uuid"
		case "name":
			sort = "name"
		case "active":
			sort = "active"
		case "target-type":
			sort = "targetType"
		case "target":
			sort = "target"
		case "create-time":
			sort = "createTime"
		case "template":
			sort = "template"
		case "severity":
			sort = "severity"
		case "destination":
			sort = "destination"
		case "alert-count":
			sort = "alertCount"
		default:
			logrus.Fatalf(
				formatter.Colorize("Invalid sorting field: "+sort+"\n", formatter.RedColor),
			)
		}

		alertCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  configuration.NewAlertConfigurationFormat(viper.GetString("output")),
		}

		var limit int32 = 10
		var offset int32 = 0

		alertAPIDirection, err := cmd.Flags().GetString("direction")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		alertAPIQuery := ybaclient.AlertConfigurationPagedApiQuery{
			Filter:    alertConfigurationAPIFilter,
			Direction: strings.ToUpper(alertAPIDirection),
			Limit:     limit,
			Offset:    offset,
			SortBy:    sort,
		}

		alertListRequest := authAPI.PageAlertConfigurations().PageAlertConfigurationsRequest(
			alertAPIQuery,
		)
		alerts := make([]ybaclient.AlertConfiguration, 0)
		force := viper.GetBool("force")
		for {
			// Execute alert list request
			r, response, err := alertListRequest.Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Alert Policy", "List")
			}

			// Check if alerts found
			if len(r.GetEntities()) < 1 {
				if util.IsOutputType(formatter.TableFormatKey) {
					logrus.Info("No alert policies found\n")
				} else {
					logrus.Info("[]\n")
				}
				return
			}

			// Write alert entities
			if force {
				alerts = append(alerts, r.GetEntities()...)
			} else {
				configuration.Write(alertCtx, r.GetEntities())
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
			alertListRequest = authAPI.PageAlertConfigurations().PageAlertConfigurationsRequest(
				alertAPIQuery,
			)
		}
		if force {
			configuration.Write(alertCtx, alerts)
		}
	},
}

func init() {
	listConfigurationAlertCmd.Flags().SortFlags = false

	listConfigurationAlertCmd.Flags().Bool("active", true,
		"[Optional] Filter active alert policy.")

	listConfigurationAlertCmd.Flags().String("destination-type", "",
		"[Optional] Destination type to filter alert policy. "+
			"Allowed values: no, default, selected")

	listConfigurationAlertCmd.Flags().String("destination", "",
		fmt.Sprintf("[Optional] Destination name to filter alert policy. %s.",
			formatter.Colorize("Required if destination-type is selected", formatter.GreenColor)))

	listConfigurationAlertCmd.Flags().String("name", "",
		"[Optional] Name to filter alert policy.")

	listConfigurationAlertCmd.Flags().String("severity", "",
		"[Optional] Severity to filter alert policy. "+
			"Allowed values: severe, warning.")

	listConfigurationAlertCmd.Flags().String("target-uuids", "",
		"[Optional] Comma separated list of target UUIDs for the alert policy.")

	listConfigurationAlertCmd.Flags().String("target-type", "",
		"[Optional] Target type to filter alert policy. "+
			"Allowed values: platform, universe.")

	listConfigurationAlertCmd.Flags().String("template", "",
		"[Optional] Template type to filter alert policy."+
			" Allowed values (case-sensitive) are listed: "+
			"https://github.com/yugabyte/yugabyte-db/blob/master/managed/"+
			"src/main/java/com/yugabyte/yw/common/AlertTemplate.java")

	listConfigurationAlertCmd.Flags().String("uuids", "",
		"[Optional] Comma separated list of alert policy UUIDs.")

	listConfigurationAlertCmd.Flags().String("sorting-field", "name",
		"[Optional] Field to sort alerts. "+
			"Allowed values: uuid, name, active, target-type, target, create-time,"+
			" template, severity, destination, alert-count.")

	listConfigurationAlertCmd.Flags().String("direction", "asc",
		"[Optional] Direction to sort alerts. "+
			"Allowed values: asc, desc.")

	listConfigurationAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
