/*
 * Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"fmt"
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

// updateConfigurationAlertCmd represents the update alert command
var updateConfigurationAlertCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an alert policy in YugabyteDB Anywhere",
	Long:    "Update an alert policy in YugabyteDB Anywhere",
	Example: `yba alert policy update --name <alert-policy-name> \
		--alert-type <alert-type> --alert-severity <alert-severity> --alert-config <alert-config>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(name) {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to update alert policy\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var existingAlertConfig ybaclient.AlertConfiguration
		alertConfigs, response, err := authAPI.ListAlertConfigurations().
			ListAlertConfigurationsRequest(
				ybaclient.AlertConfigurationApiFilter{
					Name: util.GetStringPointer(name),
				},
			).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Update - List Alert Configurations")
		}
		for _, alertConfig := range alertConfigs {
			if strings.Compare(alertConfig.GetName(), name) == 0 {
				existingAlertConfig = alertConfig
				break
			}
		}

		if util.IsEmptyString(existingAlertConfig.GetUuid()) {
			logrus.Fatal(
				formatter.Colorize(
					"Alert policy with name: "+name+" not found\n",
					formatter.RedColor))
		}

		newName, err := cmd.Flags().GetString("new-name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(newName) {
			logrus.Debug("Updating alert policy name\n")
			existingAlertConfig.SetName(newName)
		}

		description, err := cmd.Flags().GetString("description")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(description) {
			logrus.Debug("Updating alert policy description\n")
			existingAlertConfig.SetDescription(description)
		}

		if existingAlertConfig.GetTargetType() == util.UniverseAlertConfigurationTargetType &&
			cmd.Flags().Changed("target-uuids") {
			targetUUIDsString, err := cmd.Flags().GetString("target-uuids")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			targets := existingAlertConfig.GetTarget()
			if !util.IsEmptyString(targetUUIDsString) {
				targetUUIDs := strings.Split(targetUUIDsString, ",")
				logrus.Debug("Updating alert policy target uuids\n")
				targets.SetAll(false)
				targets.SetUuids(targetUUIDs)
			} else if util.IsEmptyString(targetUUIDsString) {
				logrus.Debug("Updating alert policy target to all\n")
				targets.SetAll(true)
				targets.SetUuids([]string{})
			}
			existingAlertConfig.SetTarget(targets)
		}

		threshold := existingAlertConfig.GetThresholds()
		addThresholdString, err := cmd.Flags().GetStringArray("add-threshold")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		removeThresholdString, err := cmd.Flags().GetStringArray("remove-threshold")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(removeThresholdString) > 0 {
			logrus.Debug("Removing alert policy thresholds\n")
			for _, thresholdKey := range removeThresholdString {
				delete(threshold, strings.ToUpper(thresholdKey))
			}
		}
		if len(addThresholdString) > 0 {
			logrus.Debug("Adding alert policy thresholds")
			addThresholds := buildThresholdObjectFromString(addThresholdString, "add")
			for thresholdKey, thresholdValue := range addThresholds {
				threshold[thresholdKey] = thresholdValue
			}
		}
		existingAlertConfig.SetThresholds(threshold)

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
			logrus.Debug("Updating alert policy default destination boolean\n")
			if destinationType == util.DefaultDestinationAlertConfigurationDestinationType {
				existingAlertConfig.SetDefaultDestination(true)
			} else {
				existingAlertConfig.SetDefaultDestination(false)
			}
			if destinationType == util.NoDestinationAlertConfigurationDestinationType {
				existingAlertConfig.SetDestinationUUID("")
			}
		}

		destinationUUID := ""

		destination, err := cmd.Flags().GetString("destination")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		rList, response, err := authAPI.ListAlertDestinations().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Update - List Alert Destinations")
		}
		if len(destination) != 0 {
			for _, r := range rList {
				if strings.Compare(r.GetName(), destination) == 0 {
					destinationUUID = r.GetUuid()
					break
				}
			}
			if len(destinationUUID) == 0 {
				logrus.Fatalln(
					formatter.Colorize(
						"No destination found with name: "+destination+"\n",
						formatter.RedColor))
			}
			logrus.Debug("Updating alert policy destination\n")
			existingAlertConfig.SetDestinationUUID(destinationUUID)
		} else if destinationType == util.SelectedDestinationAlertConfigurationDestinationType {
			logrus.Fatalf(
				formatter.Colorize("Destination name is required for \"selected\" destination type.\n",
					formatter.RedColor))
		}

		if cmd.Flags().Changed("duration") {
			duration, err := cmd.Flags().GetInt("duration")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debug("Updating alert policy duration\n")
			existingAlertConfig.SetDurationSec(int32(duration))
		}

		state, err := cmd.Flags().GetString("state")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(state) {
			active := false
			state = strings.ToUpper(state)
			if state == util.EnableOpType {
				active = true
			}
			logrus.Debug("Updating alert policy state\n")
			existingAlertConfig.SetActive(active)
		}

		r, response, err := authAPI.UpdateAlertConfiguration(existingAlertConfig.GetUuid()).
			UpdateAlertConfigurationRequest(existingAlertConfig).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Update")
		}

		alertConfig := util.CheckAndDereference(
			r,
			fmt.Sprintf("An error occurred while updating alert policy %s", name),
		)

		alertConfigUUID := alertConfig.GetUuid()

		if util.IsEmptyString(alertConfigUUID) {
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while updating alert policy %s\n",
					name),
				formatter.RedColor))
		}

		logrus.Infof(
			"Successfully updated alert policy %s (%s)\n",
			formatter.Colorize(name, formatter.GreenColor), alertConfigUUID)

		populateAlertDestinationAndTemplates(authAPI, "Update")

		alerts := make([]ybaclient.AlertConfiguration, 0)
		alerts = append(alerts, alertConfig)
		alertCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  configuration.NewAlertConfigurationFormat(viper.GetString("output")),
		}

		configuration.Write(alertCtx, alerts)

	},
}

func init() {
	updateConfigurationAlertCmd.Flags().SortFlags = false

	updateConfigurationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert policy to update. "+
			"Use single quotes ('') to provide values with special characters.")
	updateConfigurationAlertCmd.MarkFlagRequired("name")
	updateConfigurationAlertCmd.Flags().String("new-name", "",
		"[Optional] Update name of alert policy.")

	updateConfigurationAlertCmd.Flags().String("description", "",
		"[Optional] Update description of alert policy.")
	updateConfigurationAlertCmd.Flags().String("target-uuids", "",
		"[Optional] Comma separated list of target UUIDs for the alert policy. "+
			"If empty string is specified, the alert policy will be updated for all targets "+
			"of the target type. Allowed for target type: universe.")
	updateConfigurationAlertCmd.Flags().StringArray("add-threshold", []string{},
		"[Optional] Add or edit threshold for the configuration corresponding to severity."+
			"Each threshold needs to be added as a separate --add-threshold flag."+
			" Provide the following double colon (::) "+
			"separated fields as key-value pairs: "+
			"\"severity=<severity>::condition=<condition>::threshold=<threshold>\". "+
			"Allowed values for severity: severe, warning. "+
			"Allowed values for condition: greater-than, less-than, not-equal. Threshold should be a double. "+
			"Example: \"severity=severe::condition=greater-than::threshold=60000\".")
	updateConfigurationAlertCmd.Flags().StringArray("remove-threshold", []string{},
		"[Optional] Provide the comma separated severities to be removed from threshold."+
			" Allowed values: severe, warning.")
	updateConfigurationAlertCmd.Flags().Int("duration", 0,
		"[Optional] Update duration in seconds, while condition is met to raise an alert.")
	updateConfigurationAlertCmd.Flags().String("destination-type", "",
		"[Optional] Destination type to update alert policy. "+
			"Allowed values: no, default, selected")
	updateConfigurationAlertCmd.Flags().String("destination", "",
		fmt.Sprintf("[Optional] Destination name to send alerts. "+
			"Run \"yba alert destination list\" to check list of available destinations. %s.",
			formatter.Colorize("Required if destination-type is selected", formatter.GreenColor)))
	updateConfigurationAlertCmd.Flags().String("state", "",
		"[Optional] Set state of the alert policy. Allowed values: enable, disable.)")
}
