/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetypes

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem/instancetypes"
)

var describeInstanceTypesCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe instance type of a YugabyteDB Anywhere on-premises provider",
	Long:    "Describe instance types of a YugabyteDB Anywhere on-premises provider",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to describe instance type"+
					"\n", formatter.RedColor))
		}
		instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(instanceTypeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No instance type name found to describe"+
					"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		providerListRequest := authAPI.GetListOfProviders()

		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerListRequest = providerListRequest.Name(providerName)

		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response,
				err, "Instance Type", "Describe - Get Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		if r[0].GetCode() != util.OnpremProviderType {
			errMessage := "Operation only supported for On-premises providers."
			logrus.Fatalf(formatter.Colorize(errMessage+"\n", formatter.RedColor))
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
			errMessage := util.ErrorFromHTTPResponse(response, err, "Instance Type", "Describe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		instanceTypeList := make([]ybaclient.InstanceTypeResp, 0)
		instanceTypeList = append(instanceTypeList, rDescribe)

		if rDescribe.GetActive() {
			if len(instanceTypeList) > 0 && util.IsOutputType(formatter.TableFormatKey) {
				fullInstanceTypesContext := *instancetypes.NewFullInstanceTypesContext()
				fullInstanceTypesContext.Output = os.Stdout
				fullInstanceTypesContext.Format = instancetypes.NewFullInstanceTypesFormat(
					viper.GetString("output"))
				fullInstanceTypesContext.SetFullInstanceTypes(instanceTypeList[0])
				fullInstanceTypesContext.Write()
				return
			}

			instanceTypesCtx := formatter.Context{
				Command: "describe",
				Output:  os.Stdout,
				Format:  instancetypes.NewInstanceTypesFormat(viper.GetString("output")),
			}
			instancetypes.Write(instanceTypesCtx, instanceTypeList)
		} else {
			logrus.Infof("Instance Type %s is not active in provider %s (%s)\n",
				formatter.Colorize(instanceTypeName, formatter.GreenColor), providerName, providerUUID)
		}

	},
}

func init() {
	describeInstanceTypesCmd.Flags().SortFlags = false

	describeInstanceTypesCmd.Flags().String("instance-type-name", "",
		"[Required] Instance type name.")

	describeInstanceTypesCmd.MarkFlagRequired("instance-type-name")
}
