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
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere provider",
	Long:  "Delete a provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		providerNameFlag, err := cmd.Flags().GetString("provider-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var providerName string
		if len(providerNameFlag) > 0 {
			providerName = providerNameFlag
		} else {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to delete\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", util.ProviderType, providerName),
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
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Provider", "Delete - Fetch Providers")
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
			if len(rDelete.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for provider %s (%s) to be deleted\n",
					formatter.Colorize(providerName, formatter.GreenColor), providerUUID))
				err = authAPI.WaitForTask(rDelete.GetTaskUUID(), msg)
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
	deleteProviderCmd.Flags().StringP("provider-name", "n", "",
		"[Required] The name of the provider to be deleted.")
	deleteProviderCmd.MarkFlagRequired("provider-name")
	deleteProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
