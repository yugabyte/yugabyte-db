/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfigurationutil

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

func DeleteStorageConfigurationValidation(cmd *cobra.Command) {
	viper.BindPFlag("force", cmd.Flags().Lookup("force"))
	storageNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var storageName string
	if len(storageNameFlag) > 0 {
		storageName = storageNameFlag
	} else {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No storage configuration name found to delete\n",
				formatter.RedColor))
	}
	err = util.ConfirmCommand(
		fmt.Sprintf(
			"Are you sure you want to delete %s: %s", util.StorageConfigurationType,
			storageName),
		viper.GetBool("force"))
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
	}
}

func DeleteStorageConfigurationUtil(cmd *cobra.Command, commandCall string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	storageName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	storageConfigListRequest := authAPI.GetListOfCustomerConfig()

	r, response, err := storageConfigListRequest.Execute()
	if err != nil {
		callSite := "Storage Configuration"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			callSite,
			"Delete - List Customer Configurations")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range r {
		if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
			storageConfigs = append(storageConfigs, s)
		}
	}
	storageConfigsName := make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range storageConfigs {
		if strings.Compare(s.GetConfigName(), storageName) == 0 {
			storageConfigsName = append(storageConfigsName, s)
		}
	}
	r = storageConfigsName

	if len(r) < 1 {
		fmt.Println("No storage configurations found")
		return
	}

	var storageUUID string
	if len(r) > 0 {
		storageUUID = r[0].GetConfigUUID()
	}

	rDelete, response, err := authAPI.DeleteCustomerConfig(storageUUID).Execute()
	if err != nil {
		callSite := "Storage Configuration"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Delete")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	msg := fmt.Sprintf("The storage configuration %s is being deleted",
		formatter.Colorize(storageName, formatter.GreenColor))

	if viper.GetBool("wait") {
		if rDelete.TaskUUID != nil {
			logrus.Info(fmt.Sprintf("Waiting for storage configuration %s (%s) to be deleted\n",
				formatter.Colorize(storageName, formatter.GreenColor), storageUUID))
			err = authAPI.WaitForTask(*rDelete.TaskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		fmt.Printf("The storage configuration %s (%s) has been deleted\n",
			formatter.Colorize(storageName, formatter.GreenColor), storageUUID)
		return
	}
	fmt.Println(msg)
}
