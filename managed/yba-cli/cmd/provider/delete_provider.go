/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteProviderCmd represents the provider command
var deleteProviderCmd = &cobra.Command{
	Use:   "delete [provider-name]",
	Short: "Delete a YugabyteDB Anywhere provider",
	Long:  "Delete a provider in YugabyteDB Anywhere",
	Args:  cobra.MaximumNArgs(1),
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		providerNameFlag, _ := cmd.Flags().GetString("name")
		var providerName string
		if len(args) > 0 {
			providerName = args[0]
		} else if len(providerNameFlag) > 0 {
			providerName = providerNameFlag
		} else {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to delete\n", formatter.RedColor))
		}
		err := util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "provider", providerName),
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
		providerNameFlag, _ := cmd.Flags().GetString("name")
		var providerName string
		if len(args) > 0 {
			providerName = args[0]
		} else if len(providerNameFlag) > 0 {
			providerName = providerNameFlag
		}
		providerListRequest := authAPI.GetListOfProviders()
		providerListRequest = providerListRequest.Name(providerName)

		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "Delete")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r) < 1 {
			fmt.Println("No providers found")
			return
		}

		var providerUUID string
		if len(r) > 0 {
			providerUUID = r[0].GetUuid()
		}

		rDelete, response, err := authAPI.DeleteProvider(providerUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "Delete")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		msg := fmt.Sprintf("The provider %s is being deleted",
			formatter.Colorize(providerName, formatter.GreenColor))

		if viper.GetBool("wait") {
			if rDelete.TaskUUID != nil {
				logrus.Info(fmt.Sprintf("Waiting for provider %s (%s) to be deleted\n",
					formatter.Colorize(providerName, formatter.GreenColor), providerUUID))
				err = authAPI.WaitForTask(*rDelete.TaskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			fmt.Printf("The provider %s (%s) has been deleted\n",
				formatter.Colorize(providerName, formatter.GreenColor), providerUUID)
			return
		}
		fmt.Println(msg)
	},
}

func init() {
	deleteProviderCmd.Flags().SortFlags = false
	deleteProviderCmd.Flags().StringP("name", "n", "",
		"[Optional] The name of the provider to be deleted.")
	deleteProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
