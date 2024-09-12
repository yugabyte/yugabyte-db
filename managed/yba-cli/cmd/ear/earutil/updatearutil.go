/*
 * Copyright (c) YugaByte, Inc.
 */

package earutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ear"
	earFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ear"
)

// UpdateEARConfig is a util function to monitor update tasks
func UpdateEARConfig(
	authAPI *ybaAuthClient.AuthAPIClient,
	earName, earUUID, earCode string,
	requestBody map[string]interface{}) {
	callSite := fmt.Sprintf("EAR: %s", earCode)
	rUpdate, response, err := authAPI.EditKMSConfig(earUUID).KMSConfig(requestBody).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Update")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	taskUUID := rUpdate.GetTaskUUID()

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

		earData, response, err := authAPI.ListKMSConfigs().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Update - Fetch EAR")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		kmsConfigsCode := make([]util.KMSConfig, 0)
		for _, k := range earData {
			kmsConfig, err := util.ConvertToKMSConfig(k)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if strings.TrimSpace(earCode) != "" {
				if strings.Compare(kmsConfig.KeyProvider, earCode) == 0 {
					kmsConfigsCode = append(kmsConfigsCode, kmsConfig)
				}
			} else {
				kmsConfigsCode = append(kmsConfigsCode, kmsConfig)
			}
		}

		kmsConfigs := make([]util.KMSConfig, 0)
		if strings.TrimSpace(earName) != "" {
			for _, k := range kmsConfigsCode {
				if strings.Compare(k.Name, earName) == 0 {
					kmsConfigs = append(kmsConfigs, k)
				}
			}
		} else {
			kmsConfigs = kmsConfigsCode
		}
		earsCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  earFormatter.NewEARFormat(viper.GetString("output")),
		}

		ear.Write(earsCtx, kmsConfigs)

	} else {
		logrus.Infoln(msg + "\n")
	}

}

// GetEARConfig is a util function to get encryption at rest configuration
func GetEARConfig(
	authAPI *ybaAuthClient.AuthAPIClient, earName, earCode string) (
	util.KMSConfig, error,
) {
	kmsConfigsMap, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		callSite := fmt.Sprintf("EAR: %s", earCode)
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Update - Fetch EAR")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
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
				fmt.Sprintf("No encryption at rest configurations with name: %s and code %s found\n",
					earName, earCode),
				formatter.RedColor,
			))
	}
	return r[0], nil
}
