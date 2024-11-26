/*
 * Copyright (c) YugaByte, Inc.
 */

package keyinfo

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/keyinfo"
)

var listKeyInfoCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List runtime configuration key info",
	Long:    "List runtime configuration key info",
	Example: `yba runtime-config key-info list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		r, response, err := authAPI.ListKeyInfo().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response,
				err,
				"Runtime Configuration Key Info", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		keyinfoCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  keyinfo.NewKeyInfoFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No key infos found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		keyinfo.Write(keyinfoCtx, r)
	},
}
