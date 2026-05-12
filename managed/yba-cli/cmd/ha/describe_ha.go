/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ha

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ha"
)

var describeHACmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get", "details"},
	Short:   "Get HA configuration",
	Long:    "Get high availability configuration for YugabyteDB Anywhere",
	Example: `yba ha get`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		haConfig, response, err := authAPI.GetHAConfig().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA", "Get HA Config")
		}

		if haConfig == nil {
			logrus.Fatalf(
				formatter.Colorize("No HA config exists\n", formatter.RedColor),
			)
		}

		haCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  ha.NewHAFormat(viper.GetString("output")),
		}
		ha.Write(haCtx, haConfig)
	},
}
