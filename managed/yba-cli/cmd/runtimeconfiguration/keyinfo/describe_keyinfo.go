/*
 * Copyright (c) YugabyteDB, Inc.
 */

package keyinfo

import (
	"fmt"
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

var describeKeyInfoCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere runtime configuration key info",
	Long:    "Describe a runtime configuration key info in YugabyteDB Anywhere",
	Example: `yba runtime-config key-info describe --name <key>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		keyInfoNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(keyInfoNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No key name found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		r, response, err := authAPI.ListKeyInfo().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Runtime Configuration Key Info", "Describe")
		}

		keyInfoName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		keyInfos := make([]ybaclient.ConfKeyInfo, 0)
		for _, k := range r {
			if strings.Compare(k.GetKey(), keyInfoName) == 0 {
				keyInfos = append(keyInfos, k)
			}
		}

		if len(keyInfos) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullKeyInfoContext := *keyinfo.NewFullKeyInfoContext()
			fullKeyInfoContext.Output = os.Stdout
			fullKeyInfoContext.Format = keyinfo.NewFullKeyInfoFormat(viper.GetString("output"))
			fullKeyInfoContext.SetFullKeyInfo(keyInfos[0])
			fullKeyInfoContext.Write()
			return
		}

		if len(keyInfos) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No key infos with name: %s found\n", keyInfoName),
					formatter.RedColor,
				))
		}

		keyInfoCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  keyinfo.NewKeyInfoFormat(viper.GetString("output")),
		}
		keyinfo.Write(keyInfoCtx, keyInfos)

	},
}

func init() {
	describeKeyInfoCmd.Flags().SortFlags = false
	describeKeyInfoCmd.Flags().StringP("name", "n", "",
		"[Required] The key to be described.")
	describeKeyInfoCmd.MarkFlagRequired("name")
}
