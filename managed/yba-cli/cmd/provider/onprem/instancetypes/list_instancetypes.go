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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem/instancetypes"
)

// listInstanceTypesCmd represents the provider command
var listInstanceTypesCmd = &cobra.Command{
	Use:   "list",
	Short: "List instance types of a YugabyteDB Anywhere on-premises provider",
	Long:  "List instance types of a YugabyteDB Anywhere on-premises provider",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("provider-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to list instance types"+
					"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		providerName, err := cmd.Flags().GetString("provider-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerListRequest := authAPI.GetListOfProviders()
		providerListRequest = providerListRequest.Name(providerName)
		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Instance Type", "List - Get Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(r) < 1 {
			fmt.Println("No providers found\n")
			return
		}

		providerUUID := r[0].GetUuid()

		rList, response, err := authAPI.ListOfInstanceType(providerUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Instance Type", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		instanceTypesCtx := formatter.Context{
			Output: os.Stdout,
			Format: instancetypes.NewInstanceTypesFormat(viper.GetString("output")),
		}
		if len(rList) < 1 {
			fmt.Println("No instance types found")
			return
		}
		instancetypes.Write(instanceTypesCtx, rList)

	},
}

func init() {
	listInstanceTypesCmd.Flags().SortFlags = false

}
