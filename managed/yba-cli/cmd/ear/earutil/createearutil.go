/*
 * Copyright (c) YugaByte, Inc.
 */

package earutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ear"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// CreateEARValidation validates the delete config command
func CreateEARValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(configNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption at rest config name found to create\n",
				formatter.RedColor))
	}
}

// WaitForCreateEARTask is a util task for create ear
func WaitForCreateEARTask(
	authAPI *ybaAuthClient.AuthAPIClient,
	earName string, rTask ybaclient.YBPTask, earCode string) {

	var err error

	taskUUID := rTask.GetTaskUUID()
	earUUID := rTask.GetResourceUUID()

	earNameMessage := formatter.Colorize(earName, formatter.GreenColor)
	if len(strings.TrimSpace(earUUID)) != 0 {
		earNameMessage = fmt.Sprintf("%s (%s)", earNameMessage, earUUID)
	}

	msg := fmt.Sprintf("The encryption at rest configuration %s is being created",
		earNameMessage)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(
				fmt.Sprintf("Waiting for encryption at rest configuration %s to be created\n",
					earNameMessage))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The encryption at rest configuration %s has been created\n",
			earNameMessage)

		earData, response, err := authAPI.ListKMSConfigs().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "EAR", "Create - Fetch EAR")
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
			Command: "create",
			Output:  os.Stdout,
			Format:  ear.NewEARFormat(viper.GetString("output")),
		}

		ear.Write(earsCtx, kmsConfigs)
		return
	}
	logrus.Infoln(msg + "\n")
	taskCtx := formatter.Context{
		Command: "create",
		Output:  os.Stdout,
		Format:  ybatask.NewTaskFormat(viper.GetString("output")),
	}
	ybatask.Write(taskCtx, []ybaclient.YBPTask{rTask})

}
