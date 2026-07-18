/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ha

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

var updateHACmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update HA configuration",
	Long:    "Update high availability configuration for YugabyteDB Anywhere",
	Example: `yba ha update --uuid <uuid> [--accept-any-certificate]`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No config UUID found to update HA config\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		// Get current config to preserve values if not provided
		currentConfig, response, err := authAPI.GetHAConfig().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA", "Get HA Config")
		}

		clusterKey := currentConfig.GetClusterKey()
		acceptAnyCert := currentConfig.GetAcceptAnyCertificate()

		if cmd.Flags().Changed("accept-any-certificate") {
			acceptAnyCert, err = cmd.Flags().GetBool("accept-any-certificate")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		formData := ybaclient.HAConfigFormData{
			ClusterKey:           clusterKey,
			AcceptAnyCertificate: acceptAnyCert,
		}

		_, err = authAPI.UpdateHAConfigRest(configUUID, "HA", formData)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		logrus.Infof("The HA configuration %s has been updated\n",
			formatter.Colorize(configUUID, formatter.GreenColor))

		// Refetch and display updated config
		haConfig, response, err := authAPI.GetHAConfig().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA", "Get HA Config")
		}

		haCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  ha.NewHAFormat(viper.GetString("output")),
		}
		ha.Write(haCtx, haConfig)
	},
}

func init() {
	updateHACmd.Flags().SortFlags = false
	updateHACmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the HA configuration to update")
	updateHACmd.MarkFlagRequired("uuid")
	updateHACmd.Flags().Bool("accept-any-certificate", false,
		"[Optional] Update accept any certificate setting")
}
