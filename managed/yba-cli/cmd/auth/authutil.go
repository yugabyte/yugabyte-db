/*
 * Copyright (c) YugaByte, Inc.
 */

package auth

import (
	"fmt"
	"net/url"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/session"
)

func authWriteConfigFile(r ybaclient.SessionInfo) {
	err := viper.WriteConfig()
	if err != nil {
		if _, ok := err.(viper.ConfigFileNotFoundError); ok {
			fmt.Fprintln(os.Stdout, "No config was found a new one will be created.\n")
			//Try to create the file
			err = viper.SafeWriteConfig()
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(
						"Error when writing new config file: "+err.Error()+".\n"+
							"In case of permission errors, please run yba with --config flag to set the path.\n",
						formatter.RedColor))

			}
		} else {
			logrus.Fatalf(
				formatter.Colorize(
					"Error when writing config file: "+err.Error()+".\n", formatter.RedColor))
		}
	}
	configFileUsed := viper.GetViper().ConfigFileUsed()
	if len(configFileUsed) > 0 {
		logrus.Infof(
			formatter.Colorize(
				fmt.Sprintf("Configuration file '%v' sucessfully updated.\n",
					configFileUsed), formatter.GreenColor))
	} else {
		configFileUsed = "$HOME/.yba-cli.yaml"
		logrus.Infof(
			formatter.Colorize(
				fmt.Sprintf("Configuration file '%v' sucessfully updated.\n",
					configFileUsed), formatter.GreenColor))
	}

	sessionCtx := formatter.Context{
		Command: "auth",
		Output:  os.Stdout,
		Format:  session.NewSessionFormat(viper.GetString("output")),
	}

	session.Write(sessionCtx, []ybaclient.SessionInfo{r})
}

func authUtil(url *url.URL, apiToken string) {
	// this was established using authToken
	authAPI, err := ybaAuthClient.NewAuthAPIClientInitialize(url, apiToken)
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	r, response, err := authAPI.GetSessionInfo().Execute()
	if err != nil {
		logrus.Debugf("Full HTTP response: %v\n", response)
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"Get Session Info", "Read")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))

	}
	logrus.Debugf("Session Info response without errors\n")

	authAPI.IsCLISupported()

	// Fetch Customer UUID
	err = authAPI.GetCustomerUUID()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	logrus.Debugf("Fetched Customer UUID without errors.\n")

	viper.GetViper().Set("apiToken", apiToken)
	r.SetApiToken(apiToken)

	authWriteConfigFile(r)
}
