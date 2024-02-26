/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

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

// deleteStorageConfigurationCmd represents the storage command
var deleteStorageConfigurationCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere storage configuration",
	Long:  "Delete a storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		storageNameFlag, err := cmd.Flags().GetString("storage-config-name")
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
				"Are you sure you want to delete %s: %s", "storage configuration",
				storageName),
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
		storageName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()

		r, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response,
				err,
				"Storage Configuration",
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
			errMessage := util.ErrorFromHTTPResponse(response, err, "Storage Configuration", "Delete")
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
	},
}

func init() {
	deleteStorageConfigurationCmd.Flags().SortFlags = false
	deleteStorageConfigurationCmd.Flags().StringP("storage-config-name", "n", "",
		"[Required] The name of the storage configuration to be deleted.")
	deleteStorageConfigurationCmd.MarkFlagRequired("storage-config-name")
	deleteStorageConfigurationCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
