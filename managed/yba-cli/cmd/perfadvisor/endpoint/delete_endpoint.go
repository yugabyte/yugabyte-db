/*
 * Copyright (c) YugabyteDB, Inc.
 */

package endpoint

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteEndpointCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere Perf Advisor endpoint",
	Long: "Delete an external Perf Advisor destination. Refused while any " +
		"universe is still registered against it, and the error names those " +
		"universes.",
	Example: `yba perf-advisor endpoint delete --name byoc-prod`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, _ := cmd.Flags().GetString("name")
		err := util.ConfirmCommand(
			"Are you sure you want to delete the Perf Advisor endpoint "+name,
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatalf("%s", formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, _ := cmd.Flags().GetString("name")
		existing := findEndpoint(authAPI, name)

		response, err := authAPI.
			DeletePerfAdvisorEndpoint(existing.GetInfo().Uuid).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Perf Advisor Endpoint", "Delete")
			logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
		}

		logrus.Infof("Deleted Perf Advisor endpoint %s\n",
			formatter.Colorize(name, formatter.GreenColor))
	},
}

func init() {
	deleteEndpointCmd.Flags().SortFlags = false
	deleteEndpointCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the Perf Advisor endpoint.")
	deleteEndpointCmd.MarkFlagRequired("name")
}
