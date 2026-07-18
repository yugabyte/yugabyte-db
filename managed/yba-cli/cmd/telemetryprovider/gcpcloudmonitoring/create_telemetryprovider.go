/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcpcloudmonitoring

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createGCPCloudMonitoringTelemetryProviderCmd represents the telemetryprovider command
var createGCPCloudMonitoringTelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere GCP Cloud Monitoring telemetry provider",
	Long:    "Create a GCP Cloud Monitoring telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider gcpcloudmonitoring create --name <name> \
     --credentials <path-to-credentials-file>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.GCPCloudMonitoringTelemetryProviderType),
		}

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetName(name)

		credentialsFilePath, err := cmd.Flags().GetString("credentials-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		gcpCreds := make(map[string]interface{}, 0)
		if !util.IsEmptyString(credentialsFilePath) {
			gcpCreds, err = util.GcpGetCredentialsAsMap()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		} else {
			gcpCreds, err = util.GcpGetCredentialsAsMapFromFilePath(credentialsFilePath)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		if len(gcpCreds) == 0 {
			logrus.Fatalf(formatter.Colorize("Credentials cannot be empty.\n", formatter.RedColor))
		}

		// config.SetCredentials(gcpCreds)

		projectID, err := cmd.Flags().GetString("project-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(projectID) {
			config.SetProject(projectID)
		}

		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.GCPCloudMonitoringTelemetryProviderType, requestBody)

	},
}

func init() {
	createGCPCloudMonitoringTelemetryProviderCmd.Flags().SortFlags = false

	createGCPCloudMonitoringTelemetryProviderCmd.Flags().String("credentials-file-path", "",
		fmt.Sprintf("GCP Service Account credentials file path. "+
			"Can also be set using environment variable %s.",
			util.GCPCredentialsEnv))
	createGCPCloudMonitoringTelemetryProviderCmd.Flags().String("project-id", "",
		"[Optional] GCP Project ID. ")
	createGCPCloudMonitoringTelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")

}
