/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/instancetype"
)

// DescribeInstanceTypeUtil describes instance type
func DescribeInstanceTypeUtil(
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

	providerListRequest := authAPI.GetListOfProviders()

	providerName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	providerListRequest = providerListRequest.ProviderCode(providerType)

	providerListRequest = providerListRequest.Name(providerName)
	instancetype.ProviderName = providerName

	r, response, err := providerListRequest.Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Describe - Get Provider")
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf(
					"No %s providers with name: %s found\n",
					providerTypeInMessage,
					providerName,
				),
				formatter.RedColor,
			))
	}

	providerUUID := r[0].GetUuid()
	instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	rDescribe, response, err := authAPI.InstanceTypeDetail(
		providerUUID,
		instanceTypeName).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Instance Type", "Describe")
	}

	instanceType := util.CheckAndDereference(
		rDescribe,
		fmt.Sprintf("Instance Type: %s not found in provider: %s",
			instanceTypeName, providerName),
	)

	instanceTypeList := make([]ybaclient.InstanceTypeResp, 0)
	instanceTypeList = append(instanceTypeList, instanceType)

	if instanceType.GetActive() {
		if len(instanceTypeList) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullInstanceTypesContext := *instancetype.NewFullInstanceTypesContext()
			fullInstanceTypesContext.Output = os.Stdout
			fullInstanceTypesContext.Format = instancetype.NewFullInstanceTypesFormat(
				viper.GetString("output"))
			fullInstanceTypesContext.SetFullInstanceTypes(instanceTypeList[0])
			fullInstanceTypesContext.Write()
			return
		}

		instanceTypesCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  instancetype.NewInstanceTypesFormat(viper.GetString("output")),
		}
		instancetype.Write(instanceTypesCtx, instanceTypeList)
	} else {
		logrus.Infof("Instance Type %s is not active in provider %s (%s)\n",
			formatter.Colorize(instanceTypeName, formatter.GreenColor), providerName, providerUUID)
	}
}

// DescribeAndRemoveInstanceTypeValidations validates the flags for describe instance type
func DescribeAndRemoveInstanceTypeValidations(cmd *cobra.Command, operation string) {
	providerNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(providerNameFlag) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No provider name found to "+operation+" instance type"+
				"\n", formatter.RedColor))
	}

	instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(instanceTypeName) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No instance type name found to "+operation+
				"\n", formatter.RedColor))
	}

}
