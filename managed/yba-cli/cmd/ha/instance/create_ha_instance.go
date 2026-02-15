/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instance

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ha"
)

var createHAInstanceCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an HA platform instance",
	Long: "Create a local or remote platform instance for an HA configuration. " +
		"Run this command after the information provided in the " +
		"\"yba ha create\" commands.",
	Example: `yba ha instance create --uuid <uuid> --ip <instance-ip> --is-leader --is-local`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		instanceIP, err := cmd.Flags().GetString("ip")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"Config UUID is required to create HA instance\n",
					formatter.RedColor,
				),
			)
		}
		if util.IsEmptyString(instanceIP) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"IP Address of Instance is required to create HA instance\n",
					formatter.RedColor,
				),
			)
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		instanceIP, err := cmd.Flags().GetString("ip")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		isLeader, err := cmd.Flags().GetBool("is-leader")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		isLocal, err := cmd.Flags().GetBool("is-local")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		formData := ybaclient.PlatformInstanceFormData{
			Address:  instanceIP,
			IsLeader: isLeader,
			IsLocal:  isLocal,
		}

		instance, response, err := authAPI.CreateHAInstance(configUUID).
			PlatformInstanceFormRequest(formData).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Instance", "Create")
		}

		logrus.Infof("HA instance has been created\n")

		haCtx := formatter.Context{
			Command: "create-instance",
			Output:  os.Stdout,
			Format:  ha.NewInstancesFormat(viper.GetString("output")),
		}
		ha.WriteInstance(haCtx, instance)
	},
}

func init() {
	createHAInstanceCmd.Flags().SortFlags = false
	createHAInstanceCmd.Flags().String("uuid", "",
		"[Required] UUID of the HA configuration")
	createHAInstanceCmd.MarkFlagRequired("uuid")
	createHAInstanceCmd.Flags().String("ip", "",
		"[Required] IP address of instance (e.g., https://<ip-address or hostname>:<port>)")
	createHAInstanceCmd.MarkFlagRequired("ip")
	createHAInstanceCmd.Flags().Bool("is-leader", false,
		"[Optional] Whether this instance is the leader")
	createHAInstanceCmd.Flags().Bool("is-local", false,
		"[Optional] Whether this is the local instance")

}
