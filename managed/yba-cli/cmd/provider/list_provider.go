/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"fmt"
	"net/http"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/provider"
)

var listProviderCmd = &cobra.Command{
	Use:   "list",
	Short: "List YugabyteDB Anywhere providers",
	Long:  "List YugabyteDB Anywhere providers",
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		providerListRequest := authAPI.GetListOfProviders()
		// filter by name and/or by provider code
		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if providerName != "" {
			providerListRequest = providerListRequest.Name(providerName)
		}

		var r []ybaclient.Provider
		var response *http.Response
		providerCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if providerCode != "" {
			providerListRequest = providerListRequest.ProviderCode(providerCode)
			r, response, err = providerListRequest.Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "List")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
		} else {
			codes := []string{"aws", "gcp", "azu", "onprem", "kubernetes"}
			for _, c := range codes {
				providerListRequest = providerListRequest.ProviderCode(c)
				rCode, response, err := providerListRequest.Execute()
				if err != nil {
					errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "List")
					logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
				}
				r = append(r, rCode...)
			}
		}

		providerCtx := formatter.Context{
			Output: os.Stdout,
			Format: provider.NewProviderFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			fmt.Println("No providers found")
			return
		}
		provider.Write(providerCtx, r)

	},
}

func init() {
	listProviderCmd.Flags().SortFlags = false

	listProviderCmd.Flags().StringP("name", "n", "", "[Optional] Name of the provider.")
	listProviderCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the provider, defaults to list all providers. "+
			"Allowed values: aws, gcp, azu, onprem, kubernetes.")
}
