/*
 * Copyright (c) YugaByte, Inc.
 */

package providerutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/provider"
)

// DescribeProviderUtil executes the describe provider command
func DescribeProviderUtil(cmd *cobra.Command, commandCall, providerCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	providerListRequest := authAPI.GetListOfProviders()
	providerNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var providerName string
	if len(providerNameFlag) > 0 {
		providerName = providerNameFlag
	} else {
		logrus.Fatalln(
			formatter.Colorize("No provider name found to describe\n", formatter.RedColor))
	}
	providerListRequest = providerListRequest.Name(providerName)

	if len(strings.TrimSpace(providerCode)) != 0 {
		providerListRequest = providerListRequest.ProviderCode(providerCode)
	}

	r, response, err := providerListRequest.Execute()
	if err != nil {
		callSite := "Provider"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Describe")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	if len(r) > 0 && util.IsOutputType("table") {
		fullProviderContext := *provider.NewFullProviderContext()
		fullProviderContext.Output = os.Stdout
		fullProviderContext.Format = provider.NewFullProviderFormat(viper.GetString("output"))
		fullProviderContext.SetFullProvider(r[0])
		fullProviderContext.Write()
		return
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No providers with name: %s found\n", providerName),
				formatter.RedColor,
			))
	}

	providerCtx := formatter.Context{
		Output: os.Stdout,
		Format: provider.NewProviderFormat(viper.GetString("output")),
	}
	provider.Write(providerCtx, r)
}

// DescribeProviderValidation validates the describe provider command
func DescribeProviderValidation(cmd *cobra.Command) {
	providerNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(providerNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No provider name found to describe\n", formatter.RedColor))
	}
}
