/*
 * Copyright (c) YugabyteDB, Inc.
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
	if util.IsEmptyString(configNameFlag) {
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
	earName string, rTask *ybaclient.YBPTask, earCode string) {

	var err error

	util.CheckTaskAfterCreation(rTask)

	taskUUID := rTask.GetTaskUUID()
	earUUID := rTask.GetResourceUUID()

	earNameMessage := formatter.Colorize(earName, formatter.GreenColor)
	if !util.IsEmptyString(earUUID) {
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

		kmsConfigsList, err := authAPI.GetListOfKMSConfigs(
			"EAR", "Create - Get KMS Configurations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		kmsConfigs := KMSConfigNameAndCodeFilter(earName, earCode, kmsConfigsList)

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
	ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})

}

// KMSConfigNameAndCodeFilter filters the KMS config by name and code
func KMSConfigNameAndCodeFilter(
	earName, earCode string,
	kmsConfigs []util.KMSConfig,
) []util.KMSConfig {
	filteredKMSConfigs := make([]util.KMSConfig, 0)
	if !util.IsEmptyString(earName) {
		for _, k := range kmsConfigs {
			if strings.Compare(k.Name, earName) == 0 {
				filteredKMSConfigs = append(filteredKMSConfigs, k)
			}
		}
	} else {
		filteredKMSConfigs = kmsConfigs
	}
	if !util.IsEmptyString(earCode) {
		if strings.EqualFold(earCode, "azure") || strings.EqualFold(earCode, "az") {
			earCode = util.AzureEARType
		}
		tempKMSConfigs := make([]util.KMSConfig, 0)
		for _, k := range filteredKMSConfigs {
			if strings.Compare(k.KeyProvider, earCode) == 0 {
				tempKMSConfigs = append(tempKMSConfigs, k)
			}
		}
		filteredKMSConfigs = tempKMSConfigs
	}
	return filteredKMSConfigs
}
