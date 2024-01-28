/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetypes

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// removeInstanceTypesCmd represents the provider command
var removeInstanceTypesCmd = &cobra.Command{
	Use:     "remove",
	Aliases: []string{"delete"},
	Short:   "Delete instance type of a YugabyteDB Anywhere on-premises provider",
	Long:    "Delete instance types of a YugabyteDB Anywhere on-premises provider",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		providerNameFlag, err := cmd.Flags().GetString("provider-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to remove instance type"+
					"\n", formatter.RedColor))
		}
		instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(instanceTypeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No instance type name found to remove"+
					"\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to remove %s: %s", "instance type", instanceTypeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
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
			errMessage := util.ErrorFromHTTPResponse(response, err, "Instance Type", "Remove - Get Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(r) < 1 {
			fmt.Println("No providers found\n")
			return
		}

		providerUUID := r[0].GetUuid()

		instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rDelete, response, err := authAPI.DeleteInstanceType(providerUUID, instanceTypeName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Instance Type", "Remove")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if rDelete.GetSuccess() {
			fmt.Printf("The instance type %s has been removed from provider %s (%s)\n",
				formatter.Colorize(instanceTypeName, formatter.GreenColor), providerName, providerUUID)

		} else {
			fmt.Printf("An error occurred while removing instance type from provider")
		}
	},
}

func init() {
	removeInstanceTypesCmd.Flags().SortFlags = false

	removeInstanceTypesCmd.Flags().String("instance-type-name", "",
		"[Required] Instance type name.")
	removeInstanceTypesCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

	removeInstanceTypesCmd.MarkFlagRequired("instance-type-name")
}
