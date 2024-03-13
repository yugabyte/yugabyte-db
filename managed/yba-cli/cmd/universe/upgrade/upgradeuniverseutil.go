/*
 * Copyright (c) YugaByte, Inc.
 */

package upgrade

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
	universeFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
)

func waitForUpgradeUniverseTask(
	authAPI *ybaAuthClient.AuthAPIClient, universeName, universeUUID, taskUUID string) {

	var universeData []ybaclient.UniverseResp
	var response *http.Response
	var err error

	msg := fmt.Sprintf("The universe %s is being upgraded",
		formatter.Colorize(universeName, formatter.GreenColor))

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be upgraded\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		fmt.Printf("The universe %s (%s) has been upgraded\n",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		universeData, response, err = authAPI.ListUniverses().Name(universeName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Upgrade - Fetch Universe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		universesCtx := formatter.Context{
			Output: os.Stdout,
			Format: universeFormatter.NewUniverseFormat(viper.GetString("output")),
		}

		universeFormatter.Write(universesCtx, universeData)

	} else {
		fmt.Println(msg)
	}

}

func upgradeValidations(universeName string) (
	ybaclient.UniverseResp,
	error,
) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	universeListRequest := authAPI.ListUniverses()
	universeListRequest = universeListRequest.Name(universeName)

	r, response, err := universeListRequest.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Upgrade Software")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	if len(r) < 1 {
		err := fmt.Errorf("No universes found")
		return ybaclient.UniverseResp{}, err
	}
	return r[0], nil
}
