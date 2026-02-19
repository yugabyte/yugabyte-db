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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ear"
)

// RefreshEARValidation validates the refresh config command
func RefreshEARValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(configNameFlag) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption at rest config name found to refresh\n",
				formatter.RedColor))
	}
}

// RefreshEARUtil executes the refresh ear command
func RefreshEARUtil(cmd *cobra.Command, commandCall, earCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	earCode = strings.ToUpper(earCode)

	callSite := "EAR"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}

	earNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var earName string
	if len(earNameFlag) > 0 {
		earName = earNameFlag
	} else {
		logrus.Fatalln(
			formatter.Colorize("No configuration name found to refresh\n", formatter.RedColor))
	}

	kmsConfigsMap, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Refresh")
	}

	var r []util.KMSConfig
	if !util.IsEmptyString(earName) {
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
		if util.IsEmptyString(earCode) {
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

	r = []util.KMSConfig{r[0]}

	rRefresh, response, err := authAPI.RefreshKMSConfig(earUUID).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Refresh")
	}

	if rRefresh.GetSuccess() {
		msg := fmt.Sprintf("The configuration %s (%s) has been refreshed",
			formatter.Colorize(earName, formatter.GreenColor), earUUID)

		logrus.Infoln(msg + "\n")
	} else {
		logrus.Errorf(
			formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while refreshing configuration %s (%s)\n",
					earName, earUUID),
				formatter.RedColor))
	}

	earCtx := formatter.Context{
		Command: "refresh",
		Output:  os.Stdout,
		Format:  ear.NewEARFormat(viper.GetString("output")),
	}
	ear.Write(earCtx, r)
}
