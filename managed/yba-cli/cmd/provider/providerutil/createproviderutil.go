/*
 * Copyright (c) YugaByte, Inc.
 */

package providerutil

import (
	"fmt"
	"net/http"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	providerFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/provider"
)

// WaitForCreateProviderTask is a util task for create provider
func WaitForCreateProviderTask(
	authAPI *ybaAuthClient.AuthAPIClient,
	providerName, providerUUID, providerCode, taskUUID string) {

	var providerData []ybaclient.Provider
	var response *http.Response
	var err error

	msg := fmt.Sprintf("The provider %s (%s) is being created",
		formatter.Colorize(providerName, formatter.GreenColor), providerUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for provider %s (%s) to be created\n",
				formatter.Colorize(providerName, formatter.GreenColor), providerUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The provider %s (%s) has been created\n",
			formatter.Colorize(providerName, formatter.GreenColor), providerUUID)

		providerData, response, err = authAPI.GetListOfProviders().
			Name(providerName).ProviderCode(providerCode).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "Create - Fetch Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		providersCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  providerFormatter.NewProviderFormat(viper.GetString("output")),
		}

		providerFormatter.Write(providersCtx, providerData)

	} else {
		logrus.Infoln(msg + "\n")
	}

}

// ValueNotFoundForKeyError is throws error when value is missing for a key
func ValueNotFoundForKeyError(key string) {
	logrus.Fatalln(
		formatter.Colorize(
			fmt.Sprintf("Key \"%s\" specified but value is empty\n", key),
			formatter.RedColor))
}
