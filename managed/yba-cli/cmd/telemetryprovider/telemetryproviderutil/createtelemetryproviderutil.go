/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryproviderutil

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/telemetryprovider"
)

// CreateTelemetryProviderValidation validates the delete config command
func CreateTelemetryProviderValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(configNameFlag) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No telemetry provider name found to create\n",
				formatter.RedColor))
	}
}

// CreateTelemetryProviderUtil is a util task for create telemetryprovider
func CreateTelemetryProviderUtil(
	authAPI *ybaAuthClient.AuthAPIClient,
	telemetryProviderName, telemetryProviderType string,
	requestBody util.TelemetryProvider) {
	callSite := fmt.Sprintf("TelemetryProvider: %s", telemetryProviderType)

	telemetryProvider, err := authAPI.CreateTelemetryProviderRest(requestBody, callSite)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	telemetryProviderUUID := telemetryProvider.GetUuid()

	if util.IsEmptyString(telemetryProviderUUID) {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"An error occurred while adding telemetry provider %s\n",
				telemetryProviderName),
			formatter.RedColor))
	}

	logrus.Infof(
		"Successfully added telemetry provider %s (%s)\n",
		formatter.Colorize(telemetryProviderName, formatter.GreenColor), telemetryProviderUUID)

	r := make([]util.TelemetryProvider, 0)
	r = append(r, telemetryProvider)

	telemetryProviderCtx := formatter.Context{
		Command: "create",
		Output:  os.Stdout,
		Format:  telemetryprovider.NewTelemetryProviderFormat(viper.GetString("output")),
	}
	telemetryprovider.Write(telemetryProviderCtx, r)

}
