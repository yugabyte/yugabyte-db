/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instance

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteHAInstanceCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete an HA platform instance",
	Long:    "Delete a remote platform instance from an HA configuration (only leader can delete)",
	Example: `yba ha instance delete --uuid <uuid> --instance-uuid <instance-uuid> [--force]`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		instanceUUID, err := cmd.Flags().GetString("instance-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"Config UUID is required to delete HA instance\n",
					formatter.RedColor,
				),
			)
		}
		if util.IsEmptyString(instanceUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"Instance UUID is required to delete HA instance\n",
					formatter.RedColor,
				),
			)
		}
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete HA instance: %s", instanceUUID),
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
		instanceUUID, err := cmd.Flags().GetString("instance-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		response, err := authAPI.DeleteHAInstance(configUUID, instanceUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Instance", "Delete")
		}

		logrus.Infof("HA instance %s has been deleted\n",
			formatter.Colorize(instanceUUID, formatter.GreenColor))
	},
}

func init() {
	deleteHAInstanceCmd.Flags().SortFlags = false
	deleteHAInstanceCmd.Flags().String("uuid", "",
		"[Required] UUID of the HA configuration")
	deleteHAInstanceCmd.MarkFlagRequired("uuid")
	deleteHAInstanceCmd.Flags().String("instance-uuid", "",
		"[Required] UUID of the platform instance to delete")
	deleteHAInstanceCmd.MarkFlagRequired("instance-uuid")
	deleteHAInstanceCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

}
