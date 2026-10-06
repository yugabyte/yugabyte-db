/*
 * Copyright (c) YugabyteDB, Inc.
 */

package collector

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

// CollectorCmd inspects the Perf Advisor collector YBA scrapes universes with.
//
// Read-only: the collector YBA supports today is the embedded one, which YBA
// creates and owns itself, and whose API refuses create, edit and delete.
var CollectorCmd = &cobra.Command{
	Use:     "collector",
	Aliases: []string{"collectors"},
	Short:   "Inspect the YugabyteDB Anywhere Perf Advisor collector",
	Long: "Inspect the Perf Advisor collector that scrapes universes. The " +
		"embedded collector is managed by YBA itself, so it can be read but not " +
		"changed here.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	CollectorCmd.AddCommand(describeCollectorCmd)
}

var describeCollectorCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get", "list", "ls"},
	Short:   "Describe the YugabyteDB Anywhere Perf Advisor collectors",
	Long:    "Describe the Perf Advisor collectors configured for this customer",
	Example: `yba perf-advisor collector describe`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		collectors, response, err := authAPI.ListAllPACollectors().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Perf Advisor Collector", "Describe")
			logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
		}
		if len(collectors) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No Perf Advisor collectors found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}

		perfadvisor.WriteCollectors(formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  perfadvisor.NewCollectorFormat(viper.GetString("output")),
		}, collectors)
	},
}
