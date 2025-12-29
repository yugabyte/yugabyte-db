/*
 * Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/configuration"
)

// createConfigurationAlertCmd represents the create alert command
var createConfigurationAlertCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an alert policy in YugabyteDB Anywhere",
	Long:    "Create an alert policy in YugabyteDB Anywhere",
	Example: `yba alert policy create --name <alert-policy-name> \
		--alert-type <alert-type> --alert-severity <alert-severity> --alert-config <alert-config>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(name) {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to create alert policy\n",
					formatter.RedColor))
		}

		templateName, err := cmd.Flags().GetString("template")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(templateName) {
			logrus.Fatal(
				formatter.Colorize(
					"No template name specified to create alert policy\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		description, err := cmd.Flags().GetString("description")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		targetUUIDsString, err := cmd.Flags().GetString("target-uuids")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		targetUUIDs := make([]string, 0)
		useAllTargets := false
		if !util.IsEmptyString(targetUUIDsString) {
			targetUUIDs = strings.Split(targetUUIDsString, ",")
			useAllTargets = false
		} else {
			useAllTargets = true
		}

		thresholdString, err := cmd.Flags().GetStringArray("threshold")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		threshold := buildThresholdObjectFromString(thresholdString, "add")

		templateName, err := cmd.Flags().GetString("template")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		useDefaultAlertDestination := false

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
			if destinationType == util.DefaultDestinationAlertConfigurationDestinationType {
				useDefaultAlertDestination = true
			}
		}

		destinationUUID := ""
		destination, err := cmd.Flags().GetString("destination")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		rList, response, err := authAPI.ListAlertDestinations().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Create - List Alert Destinations")
		}
		if !util.IsEmptyString(destination) {
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
		} else if destinationType == util.SelectedDestinationAlertConfigurationDestinationType {
			logrus.Fatalf(
				formatter.Colorize("Destination name is required for \"selected\" destination type.\n",
					formatter.RedColor))
		}

		duration, err := cmd.Flags().GetInt("duration")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		templates, response, err := authAPI.ListAlertTemplates().ListTemplatesRequest(
			ybaclient.AlertTemplateApiFilter{
				Name: util.GetStringPointer(templateName),
			},
		).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Create - List Alert Templates")
		}

		if len(templates) == 0 {
			logrus.Fatalf(
				formatter.Colorize("No template found with name: "+templateName+"\n",
					formatter.RedColor))
		}

		if len(templates) > 1 {
			logrus.Fatalf(
				formatter.Colorize("More than one template found with name: "+templateName+"\n",
					formatter.RedColor))
		}

		targetType := ""
		thresholdUnit := ""
		template := ""

		if len(templates) == 1 {
			targetType = templates[0].GetTargetType()
			thresholdUnit = templates[0].GetThresholdUnit()
			template = templates[0].GetTemplate()
			if len(threshold) == 0 {
				threshold = templates[0].GetThresholds()
			}
		}

		reqBody := ybaclient.AlertConfiguration{
			CustomerUUID: authAPI.CustomerUUID,
			Name:         name,
			Description:  description,
			TargetType:   targetType,
			Target: ybaclient.AlertConfigurationTarget{
				Uuids: targetUUIDs,
				All:   util.GetBoolPointer(useAllTargets),
			},
			Thresholds:         threshold,
			ThresholdUnit:      thresholdUnit,
			Template:           template,
			DefaultDestination: useDefaultAlertDestination,
			DestinationUUID:    util.GetStringPointer(destinationUUID),
			DurationSec:        int32(duration),
			Active:             true,
		}

		r, response, err := authAPI.CreateAlertConfiguration().
			CreateAlertConfigurationRequest(reqBody).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Create")
		}

		alertConfig := util.CheckAndDereference(
			r,
			fmt.Sprintf("An error occurred while adding alert policy %s", name),
		)

		alertConfigUUID := alertConfig.GetUuid()

		if util.IsEmptyString(alertConfigUUID) {
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while adding alert policy %s\n",
					name),
				formatter.RedColor))
		}

		logrus.Infof(
			"Successfully added alert policy %s (%s)\n",
			formatter.Colorize(name, formatter.GreenColor), alertConfigUUID)

		populateAlertDestinationAndTemplates(authAPI, "Create")

		alerts := make([]ybaclient.AlertConfiguration, 0)
		alerts = append(alerts, alertConfig)
		alertCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  configuration.NewAlertConfigurationFormat(viper.GetString("output")),
		}

		configuration.Write(alertCtx, alerts)

	},
}

func init() {
	createConfigurationAlertCmd.Flags().SortFlags = false

	createConfigurationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert policy to create. "+
			"Use single quotes ('') to provide values with special characters.")
	createConfigurationAlertCmd.MarkFlagRequired("name")
	createConfigurationAlertCmd.Flags().String("description", "",
		"[Optional] Description of alert policy to create.")
	createConfigurationAlertCmd.Flags().String("target-uuids", "",
		"[Optional] Comma separated list of target UUIDs for the alert policy. "+
			"If left empty, the alert policy will be created for all targets "+
			"of the target type. Allowed for target type: universe.")
	createConfigurationAlertCmd.Flags().StringArray("threshold", []string{},
		"[Optional] Threshold for the configuration corresponding to severity. "+
			"Each threshold needs to be added as a separate --threshold flag."+
			"Provide the following double colon (::) "+
			"separated fields as key-value pairs: "+
			"\"severity=<severity>::condition=<condition>::threshold=<threshold>\". "+
			"Allowed values for severity: severe, warning. "+
			"Allowed values for condition: greater-than, less-than, not-equal. Threshold should be a double. "+
			"Example: \"severity=severe::condition=greater-than::threshold=60000\".")
	createConfigurationAlertCmd.Flags().String("template", "",
		"[Required] Template name for the alert policy. "+
			"Use single quotes ('') to provide values with special characters. "+
			"Run \"yba alert policy template list\" to check list of available template names.")
	createConfigurationAlertCmd.MarkFlagRequired("template")
	createConfigurationAlertCmd.Flags().Int("duration", 0,
		"[Optional] Duration in seconds, while condition is met to raise an alert. (default 0)")
	createConfigurationAlertCmd.Flags().String("destination-type", "",
		"[Optional] Destination type to create alert policy. "+
			"Allowed values: no, default, selected")
	createConfigurationAlertCmd.Flags().String("destination", "",
		fmt.Sprintf("[Optional] Destination name to send alerts. "+
			"Run \"yba alert destination list\" to check list of available destinations. %s.",
			formatter.Colorize("Required if destination-type is selected", formatter.GreenColor)))
}

func buildThresholdObjectFromString(thresholdStrings []string, operation string,
) (res map[string]ybaclient.AlertConfigurationThreshold) {
	res = map[string]ybaclient.AlertConfigurationThreshold{}
	if len(thresholdStrings) == 0 {
		return res
	}
	for _, thresholdString := range thresholdStrings {
		thresholdMap := map[string]string{}
		for _, thresholdInfo := range strings.Split(thresholdString, util.Separator) {
			kvp := strings.Split(thresholdInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in threshold description.\n",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "severity":
				if !util.IsEmptyString(val) {
					thresholdMap["severity"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "condition":
				if !util.IsEmptyString(val) {
					thresholdMap["condition"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "threshold":
				if !util.IsEmptyString(val) {
					thresholdMap["threshold"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			}
		}
		if _, ok := thresholdMap["severity"]; !ok {
			util.MissingKeyFromStringDeclaration(
				"severity", "threshold")
		}
		thresholdMap["severity"] = strings.ToUpper(thresholdMap["severity"])

		if strings.Compare(operation, "add") == 0 {
			if _, ok := thresholdMap["condition"]; !ok {
				util.MissingKeyFromStringDeclaration(
					"condition", "threshold")
			}

			if _, ok := thresholdMap["threshold"]; !ok {
				util.MissingKeyFromStringDeclaration(
					"threshold", "threshold")
			}
		}
		condition := ""
		switch thresholdMap["condition"] {
		case "greater-than":
			condition = "GREATER_THAN"
		case "less-than":
			condition = "LESS_THAN"
		case "not-equal":
			condition = "NOT_EQUAL"
		}
		threshold, err := strconv.ParseFloat(thresholdMap["threshold"], 64)
		if err != nil {
			errMessage := err.Error() +
				" Invalid or missing value provided for 'threshold'. Setting it to '0.0'.\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			threshold = 0.0
		}

		res[thresholdMap["severity"]] = ybaclient.AlertConfigurationThreshold{
			Condition: condition,
			Threshold: threshold,
		}
	}
	return res
}
