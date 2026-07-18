/*
 * Copyright (c) YugabyteDB, Inc.
 */

package key

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// SetKey sets a key in the scope
func SetKey(
	authAPI *ybaAuthClient.AuthAPIClient,
	scope string,
	key string,
	value string,
	logSuccess bool,
) {
	r, response, err := authAPI.SetKey(scope, key).NewValue(value).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"Runtime Configuration Scope Key", "Set")
		logrus.Fatalf("%s", formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	if r.GetSuccess() {
		if logSuccess {
			logrus.Info(fmt.Sprintf("The key %s of scope %s has been updated\n",
				formatter.Colorize(key, formatter.GreenColor), scope))
		}

	} else {
		errorMessage := formatter.Colorize(
			fmt.Sprintf(
				"An error occurred while updating key %s\n",
				formatter.Colorize(key, formatter.GreenColor)),
			formatter.RedColor)
		logrus.Errorf("%s", errorMessage)
	}
}

// DeleteKey deletes a key from the scope
func DeleteKey(authAPI *ybaAuthClient.AuthAPIClient, scope string, key string, logSuccess bool) {
	r, response, err := authAPI.DeleteKey(scope, key).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"Runtime Configuration Scope Key", "Delete")
		logrus.Fatalf("%s", formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	if r.GetSuccess() {
		if logSuccess {
			logrus.Info(fmt.Sprintf("The key %s of scope %s has been deleted\n",
				formatter.Colorize(key, formatter.GreenColor), scope))
		}
	} else {
		errorMessage := formatter.Colorize(
			fmt.Sprintf(
				"An error occurred while deleting key %s\n",
				formatter.Colorize(key, formatter.GreenColor)),
			formatter.RedColor)
		logrus.Errorf("%s", errorMessage)
	}
}

// CheckAndSetGlobalKey checks if the value is not empty and sets the key in the global scope
func CheckAndSetGlobalKey(authAPI *ybaAuthClient.AuthAPIClient, keyName, value string) {
	if value != "" {
		SetKey(authAPI, util.GlobalScopeUUID, keyName, value, false /*logSuccess*/)
	}
}

// DeleteGlobalKey deletes the key from the global scope
func DeleteGlobalKey(authAPI *ybaAuthClient.AuthAPIClient, keyName string) {
	DeleteKey(authAPI, util.GlobalScopeUUID, keyName, false /*logSuccess*/)
}
