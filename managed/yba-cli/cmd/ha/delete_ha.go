/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ha

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteHACmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete HA configuration",
	Long:    "Delete high availability configuration for YugabyteDB Anywhere",
	Example: `yba ha delete --uuid <uuid> [--force]`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No config UUID found to delete HA config\n",
					formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete HA configuration: %s", configUUID),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		response, err := authAPI.DeleteHAConfig(configUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA", "Delete")
		}

		logrus.Infof("The HA configuration %s has been deleted\n",
			formatter.Colorize(configUUID, formatter.GreenColor))
	},
}

func init() {
	deleteHACmd.Flags().SortFlags = false
	deleteHACmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the HA configuration to delete")
	deleteHACmd.MarkFlagRequired("uuid")
	deleteHACmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
