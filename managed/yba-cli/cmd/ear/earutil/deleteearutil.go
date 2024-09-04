/*
 * Copyright (c) YugaByte, Inc.
 */

package earutil

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// DeleteEARValidation validates the delete config command
func DeleteEARValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(configNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption at rest config name found to delete\n",
				formatter.RedColor))
	}
	err = util.ConfirmCommand(
		fmt.Sprintf(
			"Are you sure you want to delete %s: %s", "encryption at rest configuration",
			configNameFlag),
		viper.GetBool("force"))
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

// DeleteEARUtil executes the delete ear command
func DeleteEARUtil(cmd *cobra.Command, commandCall, earCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	earCode = strings.ToUpper(earCode)

	earNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var earName string
	if len(earNameFlag) > 0 {
		earName = earNameFlag
	} else {
		logrus.Fatalln(
			formatter.Colorize("No configuration name found to delete\n", formatter.RedColor))
	}

	kmsConfigsMap, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		callSite := "EAR"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Delete - List KMS Configs")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	var r []util.KMSConfig
	if strings.TrimSpace(earName) != "" {
		for _, kmsConfig := range kmsConfigsMap {
			k, err := util.ConvertToKMSConfig(kmsConfig)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if strings.Compare(k.Name, earName) == 0 {
				if earCode != "" {
					if strings.Compare(k.KeyProvider, earCode) == 0 {
						r = append(r, k)
					}
				} else {
					r = append(r, k)
				}

			}
		}
	}

	if len(r) < 1 {
		errMessage := ""
		if len(strings.TrimSpace(earCode)) == 0 {
			errMessage = fmt.Sprintf("No configurations with name: %s found\n", earName)
		} else {
			errMessage = fmt.Sprintf(
				"No configurations with name: %s and type: %s found\n", earName, earCode)
		}
		logrus.Fatalf(
			formatter.Colorize(
				errMessage,
				formatter.RedColor,
			))
	}

	earUUID := r[0].ConfigUUID
	rDelete, response, err := authAPI.DeleteKMSConfig(earUUID).Execute()
	if err != nil {
		callSite := "EAR"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Delete")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	msg := fmt.Sprintf("The encryption at rest configuration %s (%s) is being deleted",
		formatter.Colorize(earName, formatter.GreenColor), earUUID)

	if viper.GetBool("wait") {
		if len(rDelete.GetTaskUUID()) > 0 {
			logrus.Info(fmt.Sprintf(
				"Waiting for encryption at rest configuration %s (%s) to be deleted\n",
				formatter.Colorize(earName, formatter.GreenColor), earUUID))
			err = authAPI.WaitForTask(rDelete.GetTaskUUID(), msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The encryption at rest configuration %s (%s) has been deleted\n",
			formatter.Colorize(earName, formatter.GreenColor), earUUID)
		return
	}
	logrus.Infoln(msg + "\n")
}
