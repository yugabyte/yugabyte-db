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

// DescribeTelemetryProviderUtil executes the describe telemetry provider command
func DescribeTelemetryProviderUtil(
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
		authAPI, callSite, "Describe", telemetryProviderName, telemetryProviderType)

	if len(r) < 1 {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintf("No telemetry provider with name %s found\n", telemetryProviderName),
			formatter.RedColor))
	}

	if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
		fullTelemetryProviderContext := *telemetryprovider.NewFullTelemetryProviderContext()
		fullTelemetryProviderContext.Output = os.Stdout
		fullTelemetryProviderContext.Format = telemetryprovider.NewFullTelemetryProviderFormat(
			viper.GetString("output"),
		)
		fullTelemetryProviderContext.SetFullTelemetryProvider(r[0])
		fullTelemetryProviderContext.Write()
		return
	}

	telemetryProviderCtx := formatter.Context{
		Command: "describe",
		Output:  os.Stdout,
		Format:  telemetryprovider.NewTelemetryProviderFormat(viper.GetString("output")),
	}
	telemetryprovider.Write(telemetryProviderCtx, r)
}

// DescribeTelemetryProviderValidation validates the describe telemetry provider command
func DescribeTelemetryProviderValidation(cmd *cobra.Command) {
	telemetryProviderNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(telemetryProviderNameFlag) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No telemetry provider name found to describe\n",
				formatter.RedColor))
	}
}
