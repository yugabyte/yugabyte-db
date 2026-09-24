/*
 * Copyright (c) YugabyteDB, Inc.
 */

package endpoint

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/perfadvisor"
)

var listEndpointCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere Perf Advisor endpoints",
	Long:    "List the external Perf Advisor destinations configured for this customer",
	Example: `yba perf-advisor endpoint list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf("%s", formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		endpoints := listEndpoints(authAPI, name)
		if len(endpoints) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No Perf Advisor endpoints found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}

		perfadvisor.Write(formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  perfadvisor.NewEndpointFormat(viper.GetString("output")),
		}, endpoints)
	},
}

func init() {
	listEndpointCmd.Flags().SortFlags = false
	listEndpointCmd.Flags().StringP("name", "n", "",
		"[Optional] Name of the Perf Advisor endpoint.")
}
