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

// ListEARUtil executes the list ear command
func ListEARUtil(cmd *cobra.Command, commandCall, earCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	earCode = strings.ToUpper(earCode)
	callSite := "EAR"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}

	kmsConfigsList, err := authAPI.GetListOfKMSConfigs(callSite, "List")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	// filter by name and/or by ear code
	earName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	kmsConfigs := KMSConfigNameAndCodeFilter(earName, earCode, kmsConfigsList)

	earCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  ear.NewEARFormat(viper.GetString("output")),
	}
	if len(kmsConfigs) < 1 {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Info("No encryption at rest configurations found\n")
		} else {
			logrus.Info("[]\n")
		}
		return
	}
	ear.Write(earCtx, kmsConfigs)
}
