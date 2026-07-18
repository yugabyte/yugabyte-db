/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

import (
	"fmt"
	"os"
	"sort"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/instancetype"
)

// ListInstanceTypeUtil lists
func ListInstanceTypeUtil(
	cmd *cobra.Command,
	providerType string,
	providerTypeInLog string,
	providerTypeInMessage string,
) {
	callSite := "Instance Type"
	if len(providerTypeInLog) > 0 {
		callSite = fmt.Sprintf("%s: %s", callSite, providerTypeInLog)
	}
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	providerName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	providerListRequest := authAPI.GetListOfProviders().ProviderCode(providerType)

	instancetype.ProviderName = providerName
	providerListRequest = providerListRequest.Name(providerName)

	r, response, err := providerListRequest.Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "List - Get Provider")
	}
	if len(r) < 1 {
		logrus.Fatalf(
			"No %s providers with name: %s found\n",
			providerTypeInMessage,
			providerName,
		)
	}

	providerUUID := r[0].GetUuid()

	rList, response, err := authAPI.ListOfInstanceType(providerUUID).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "List")
	}

	if len(rList) < 1 {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Infoln("No instance types found\n")
		} else {
			logrus.Infoln("[]\n")
		}
		return
	}

	sort.SliceStable(rList, func(i, j int) bool {
		iName := rList[i].GetInstanceTypeCode()
		jName := rList[j].GetInstanceTypeCode()
		return strings.ToLower(iName) < strings.ToLower(jName)
	})

	instanceTypesCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  instancetype.NewInstanceTypesFormat(viper.GetString("output")),
	}

	instancetype.Write(instanceTypesCtx, rList)

}
