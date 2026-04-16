/*
 * Copyright (c) YugabyteDB, Inc.
 */

package keyinfo

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"

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
			util.FatalHTTPError(response, err, "Runtime Configuration Key Info", "List")
		}

		scopeType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scopeType) > 0 {
			rList := make([]ybaclient.ConfKeyInfo, 0)
			for _, keyInfo := range r {
				if strings.EqualFold(keyInfo.GetScope(), scopeType) {
					rList = append(rList, keyInfo)
				}
			}
			r = rList
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

func init() {
	listKeyInfoCmd.Flags().SortFlags = false
	listKeyInfoCmd.Flags().
		String("type", "", "[Optional] Scope type. Allowed values: universe, customer, provider, global.")
}
