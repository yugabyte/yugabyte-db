/*
 * Copyright (c) YugaByte, Inc.
 */

package providerutil

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/provider"
)

func ListProviderUtil(cmd *cobra.Command, commandCall, providerCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

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

	if len(strings.TrimSpace(providerCode)) != 0 {
		providerListRequest = providerListRequest.ProviderCode(providerCode)
		r, response, err = providerListRequest.Execute()
		callSite := "Provider"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
	} else if len(strings.TrimSpace(commandCall)) == 0 {
		codes := []string{
			util.AWSProviderType,
			util.GCPProviderType,
			util.AzureProviderType,
			util.OnpremProviderType,
			util.K8sProviderType}
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
		if util.IsOutputType("table") {
			logrus.Infoln("No providers found\n")
		} else {
			logrus.Infoln("{}\n")
		}
		return
	}
	provider.Write(providerCtx, r)
}
