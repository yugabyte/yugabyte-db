/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetype

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// SupportedStorageInstanceTypeCmd represents the supported-storage command
var SupportedStorageInstanceTypeCmd = &cobra.Command{
	Use:     "supported-storage",
	Aliases: []string{"supportedstorage", "supported-storage-types"},
	Short:   "List supported storage types for a YugabyteDB Anywhere AWS (EBS) instance type",
	Long:    "List supported storage types for a YugabyteDB Anywhere AWS (EBS) instance type",
	Example: `yba provider aws instance-type supported-storage`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		types, response, err := authAPI.GetEBSTypes().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response,
				err,
				"Instance Type",
				"List - Get EBS Types",
			)
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		typesString := util.GetPrintableList(types)

		logrus.Infof(
			"Supported storage for YugabyteDB Anywhere AWS (EBS) instance types: %v\n",
			typesString,
		)
	},
}
