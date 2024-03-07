/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/provider"
)

var describeProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere provider",
	Long:    "Describe a provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("provider-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		providerListRequest := authAPI.GetListOfProviders()
		providerNameFlag, _ := cmd.Flags().GetString("provider-name")
		var providerName string
		if len(providerNameFlag) > 0 {
			providerName = providerNameFlag
		} else {
			logrus.Fatalln(
				formatter.Colorize("No provider name found to describe\n", formatter.RedColor))
		}
		providerListRequest = providerListRequest.Name(providerName)

		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "Describe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r) > 0 && viper.GetString("output") == "table" {
			fullProviderContext := *provider.NewFullProviderContext()
			fullProviderContext.Output = os.Stdout
			fullProviderContext.Format = provider.NewFullProviderFormat(viper.GetString("output"))
			fullProviderContext.SetFullProvider(r[0])
			fullProviderContext.Write()
			return
		}

		if len(r) < 1 {
			fmt.Println("No providers found")
			return
		}

		providerCtx := formatter.Context{
			Output: os.Stdout,
			Format: provider.NewProviderFormat(viper.GetString("output")),
		}
		provider.Write(providerCtx, r)

	},
}

func init() {
	describeProviderCmd.Flags().SortFlags = false
	describeProviderCmd.Flags().StringP("provider-name", "n", "",
		"[Required] The name of the provider to get details.")
	describeProviderCmd.MarkFlagRequired("provider-name")
}
