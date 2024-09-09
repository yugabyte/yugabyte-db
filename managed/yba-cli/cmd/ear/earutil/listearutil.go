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

// ListEARUtil executes the list ear command
func ListEARUtil(cmd *cobra.Command, commandCall, earCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	earCode = strings.ToUpper(earCode)
	callSite := "EAR"
	if len(strings.TrimSpace(commandCall)) != 0 {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}
	r, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "List")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	// filter by name and/or by ear code
	earName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	kmsConfigsCode := make([]util.KMSConfig, 0)
	for _, k := range r {
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

	earCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  ear.NewEARFormat(viper.GetString("output")),
	}
	if len(kmsConfigs) < 1 {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Infoln("No encryption at rest configurations found\n")
		} else {
			logrus.Infoln("[]\n")
		}
		return
	}
	ear.Write(earCtx, kmsConfigs)
}
