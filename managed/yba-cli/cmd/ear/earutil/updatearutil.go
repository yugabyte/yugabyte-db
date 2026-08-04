/*
 * Copyright (c) YugabyteDB, Inc.
 */

package earutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ear"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// UpdateEARConfig is a util function to monitor update tasks
func UpdateEARConfig(
	authAPI *ybaAuthClient.AuthAPIClient,
	earName, earUUID, earCode string,
	requestBody map[string]interface{}) {
	callSite := fmt.Sprintf("EAR: %s", earCode)
	rTask, response, err := authAPI.EditKMSConfig(earUUID).KMSConfig(requestBody).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Update")
	}

	util.CheckTaskAfterCreation(rTask)

	taskUUID := rTask.GetTaskUUID()

	msg := fmt.Sprintf("The ear %s (%s) is being updated",
		formatter.Colorize(earName, formatter.GreenColor), earUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for encryption at rest configuration"+
				" %s (%s) to be updated\n",
				formatter.Colorize(earName, formatter.GreenColor), earUUID))
			err := authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The encryption at rest configuration %s (%s) has been updated\n",
			formatter.Colorize(earName, formatter.GreenColor), earUUID)

		kmsConfigsList, err := authAPI.GetListOfKMSConfigs(
			"EAR", "Update - Get KMS Configurations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		kmsConfigs := KMSConfigNameAndCodeFilter(earName, earCode, kmsConfigsList)

		earsCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  ear.NewEARFormat(viper.GetString("output")),
		}

		ear.Write(earsCtx, kmsConfigs)
		return
	}
	logrus.Infoln(msg + "\n")

	taskCtx := formatter.Context{
		Command: "update",
		Output:  os.Stdout,
		Format:  ybatask.NewTaskFormat(viper.GetString("output")),
	}
	ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})

}

// GetEARConfig is a util function to get encryption at rest configuration
func GetEARConfig(
	authAPI *ybaAuthClient.AuthAPIClient, earName, earCode string) (
	util.KMSConfig, error,
) {
	kmsConfigsMap, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		callSite := fmt.Sprintf("EAR: %s", earCode)
		util.FatalHTTPError(response, err, callSite, "Update - Fetch EAR")
	}
	var r []util.KMSConfig

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
	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf(
					"No encryption at rest configurations with name: %s and code %s found\n",
					earName,
					earCode,
				),
				formatter.RedColor,
			))
	}
	return r[0], nil
}
