/*
 * Copyright (c) YugabyteDB, Inc.
 */

package providerutil

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// DeleteProviderValidation validates the delete provider command
func DeleteProviderValidation(cmd *cobra.Command) {
	viper.BindPFlag("force", cmd.Flags().Lookup("force"))
	providerNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var providerName string
	if !util.IsEmptyString(providerNameFlag) {
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
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

// DeleteProviderUtil deletes a provider
func DeleteProviderUtil(cmd *cobra.Command, commandCall, providerCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	providerName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	providerListRequest := authAPI.GetListOfProviders()
	providerListRequest = providerListRequest.Name(providerName)

	if !util.IsEmptyString(providerCode) {
		providerListRequest = providerListRequest.ProviderCode(providerCode)
	}

	r, response, err := providerListRequest.Execute()
	if err != nil {
		callSite := "Provider"
		if !util.IsEmptyString(commandCall) {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		util.FatalHTTPError(response, err, callSite, "Delete - Fetch Providers")
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No providers with name: %s found\n", providerName),
				formatter.RedColor,
			))
	}

	var providerUUID string
	if len(r) > 0 {
		providerUUID = r[0].GetUuid()
	}

	rTask, response, err := authAPI.DeleteProvider(providerUUID).Execute()
	if err != nil {
		callSite := "Provider"
		if !util.IsEmptyString(commandCall) {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		util.FatalHTTPError(response, err, callSite, "Delete")
	}

	util.CheckTaskAfterCreation(rTask)

	msg := fmt.Sprintf("The provider %s (%s) is being deleted",
		formatter.Colorize(providerName, formatter.GreenColor), providerUUID)

	if viper.GetBool("wait") {
		if len(rTask.GetTaskUUID()) > 0 {
			logrus.Info(fmt.Sprintf("Waiting for provider %s (%s) to be deleted\n",
				formatter.Colorize(providerName, formatter.GreenColor), providerUUID))
			err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The provider %s (%s) has been deleted\n",
			formatter.Colorize(providerName, formatter.GreenColor), providerUUID)
		return
	}
	logrus.Infoln(msg + "\n")
	taskCtx := formatter.Context{
		Command: "delete",
		Output:  os.Stdout,
		Format:  ybatask.NewTaskFormat(viper.GetString("output")),
	}
	ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})
}
