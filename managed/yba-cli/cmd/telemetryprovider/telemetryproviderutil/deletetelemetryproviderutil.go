/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryproviderutil

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// DeleteTelemetryProviderUtil executes the delete telemetry provider command
func DeleteTelemetryProviderUtil(
	cmd *cobra.Command,
	commandCall string,
	telemetryProviderType string,
) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	VersionCheck(authAPI)

	callSite := "Telemetry Provider"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}

	telemetryProviderName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	r := ListAndFilterTelemetryProviders(
		authAPI,
		callSite,
		"Delete - List Telemetry Providers",
		telemetryProviderName,
		telemetryProviderType,
	)

	if len(r) < 1 {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintf("No telemetry provider with name %s found\n", telemetryProviderName),
			formatter.RedColor))
	}

	telemetryProvider := r[0]
	telemetryProviderUUID := telemetryProvider.GetUuid()

	rDelete, response, err := authAPI.DeleteTelemetryProvider(telemetryProviderUUID).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Delete")
	}

	if rDelete.GetSuccess() {
		msg := fmt.Sprintf("The telemetry provider %s (%s) has been deleted",
			formatter.Colorize(telemetryProviderName, formatter.GreenColor), telemetryProviderUUID)

		logrus.Infoln(msg + "\n")
	} else {
		logrus.Errorf(
			formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while removing telemetry provider %s (%s)\n",
					telemetryProviderName, telemetryProviderUUID),
				formatter.RedColor))
	}
}

// DeleteTelemetryProviderValidation validates the delete telemetry provider command
func DeleteTelemetryProviderValidation(cmd *cobra.Command) {
	viper.BindPFlag("force", cmd.Flags().Lookup("force"))
	telemetryProviderNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(telemetryProviderNameFlag) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No telemetry provider name found to delete\n",
				formatter.RedColor))
	}
	err = util.ConfirmCommand(
		fmt.Sprintf(
			"Are you sure you want to delete %s: %s", "telemetry provider",
			telemetryProviderNameFlag),
		viper.GetBool("force"))
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

// VersionCheck checks if the YBA version supports the telemetry provider operations
func VersionCheck(authAPI *ybaAuthClient.AuthAPIClient) {
	allowed, version, err := authAPI.TelemetryProviderYBAVersionCheck()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if !allowed {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintf("Telemetry Provider operations below version %s (or on restricted"+
				" versions) is not supported, currently on %s\n", util.YBAAllowTelemetryProviderMinStableVersion,
				version), formatter.RedColor))
	}
}
