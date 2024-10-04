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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ear"
)

// DescribeEARValidation validates the describe config command
func DescribeEARValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(configNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption at rest config name found to describe\n",
				formatter.RedColor))
	}
}

// DescribeEARUtil executes the describe ear command
func DescribeEARUtil(cmd *cobra.Command, commandCall, earCode string) {
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
			formatter.Colorize("No configuration name found to describe\n", formatter.RedColor))
	}

	kmsConfigsMap, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		callSite := "EAR"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Describe")
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

	if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
		fullEARContext := *ear.NewFullEARContext()
		fullEARContext.Output = os.Stdout
		fullEARContext.Format = ear.NewFullEARFormat(viper.GetString("output"))
		fullEARContext.SetFullEAR(r[0])
		fullEARContext.Write()
		return
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No configurations with name: %s found\n", earName),
				formatter.RedColor,
			))
	}

	earCtx := formatter.Context{
		Command: "describe",
		Output:  os.Stdout,
		Format:  ear.NewEARFormat(viper.GetString("output")),
	}
	ear.Write(earCtx, r)
}
