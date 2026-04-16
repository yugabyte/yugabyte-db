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

var createHACmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create HA configuration",
	Long: "Create high availability configuration for YugabyteDB Anywhere. " +
		"Run this command after genereating the cluster key using \"yba ha generate-cluster-key\" command",
	Example: `yba ha create --cluster-key <cluster-key> [--accept-any-certificate]`,
	PreRun: func(cmd *cobra.Command, args []string) {
		clusterKey, err := cmd.Flags().GetString("cluster-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if util.IsEmptyString(clusterKey) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No cluster key found to create HA config\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		clusterKey, err := cmd.Flags().GetString("cluster-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		acceptAnyCert, err := cmd.Flags().GetBool("accept-any-certificate")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		formData := ybaclient.HAConfigFormData{
			ClusterKey:           clusterKey,
			AcceptAnyCertificate: acceptAnyCert,
		}

		haConfig, response, err := authAPI.CreateHAConfig().HAConfigFormRequest(formData).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA", "Create HA Config")
		}

		if haConfig == nil {
			logrus.Fatalf(
				formatter.Colorize("Failed to create HA config\n", formatter.RedColor),
			)
		}

		logrus.Infof("The HA configuration %s has been created\n",
			formatter.Colorize(haConfig.Uuid, formatter.GreenColor))

		// Refetch and display updated config
		highAvailabilityConfig, response, err := authAPI.GetHAConfig().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA", "Get HA Config")
		}

		haCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  ha.NewHAFormat(viper.GetString("output")),
		}
		ha.Write(haCtx, highAvailabilityConfig)
	},
}

func init() {
	createHACmd.Flags().SortFlags = false
	createHACmd.Flags().String("cluster-key", "",
		"[Required] The cluster key for HA configuration (must be 44 characters)")
	createHACmd.MarkFlagRequired("cluster-key")
	createHACmd.Flags().Bool("accept-any-certificate", true,
		"[Optional] Accept any certificate for HA connections (default true)")

}
