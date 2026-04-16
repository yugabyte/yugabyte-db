/*
 * Copyright (c) YugabyteDB, Inc.
 */

package awscloudwatch

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createAWSCloudWatchTelemetryProviderCmd represents the telemetryprovider command
var createAWSCloudWatchTelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere AWS CloudWatch telemetry provider",
	Long:    "Create a AWS CloudWatch telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider awscloudwatch create --name <name> \
    --access-key-id <access-key-id> --secret-access-key <secret-access-key> --region <region> \
    --log-group <log-group> --log-stream <log-stream>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.AWSCloudWatchTelemetryProviderType),
		}

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetName(name)

		accessKeyID, err := cmd.Flags().GetString("access-key-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(accessKeyID) == 0 && len(secretAccessKey) == 0 {
			awsCreds, err := util.AwsCredentialsFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			accessKeyID = awsCreds.AccessKeyID
			secretAccessKey = awsCreds.SecretAccessKey
		}
		if len(accessKeyID) == 0 || len(secretAccessKey) == 0 {
			logrus.Fatalf(
				formatter.Colorize(
					"AWS Access Key ID and Secret Access Key are required\n",
					formatter.RedColor,
				),
			)
		}
		config.SetAccessKey(accessKeyID)
		config.SetSecretKey(secretAccessKey)

		logGroup, err := cmd.Flags().GetString("log-group")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		config.SetLogGroup(logGroup)

		logStream, err := cmd.Flags().GetString("log-stream")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		config.SetLogStream(logStream)

		region, err := cmd.Flags().GetString("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(region) {
			region, err = util.AWSRegionFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		if util.IsEmptyString(region) {
			logrus.Fatalf(formatter.Colorize("AWS region is required\n", formatter.RedColor))
		}

		config.SetRegion(region)

		roleARN, err := cmd.Flags().GetString("role-arn")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(roleARN) {
			config.SetRoleARN(roleARN)
		}

		endpoint, err := cmd.Flags().GetString("endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(endpoint) {
			config.SetEndpoint(endpoint)
		}

		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.AWSCloudWatchTelemetryProviderType, requestBody)

	},
}

func init() {
	createAWSCloudWatchTelemetryProviderCmd.Flags().SortFlags = false

	createAWSCloudWatchTelemetryProviderCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("AWS Access Key ID. "+
			"Can also be set using environment variable %s.",
			util.AWSAccessKeyEnv))
	createAWSCloudWatchTelemetryProviderCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("AWS Secret Access Key. "+
			"Can also be set using environment variable %s.",
			util.AWSSecretAccessKeyEnv))
	createAWSCloudWatchTelemetryProviderCmd.MarkFlagsRequiredTogether(
		"access-key-id",
		"secret-access-key",
	)
	createAWSCloudWatchTelemetryProviderCmd.Flags().String("region", "",
		fmt.Sprintf("AWS region. "+
			"Can also be set using environment variable %s",
			util.AWSRegionEnv))

	createAWSCloudWatchTelemetryProviderCmd.Flags().
		String("log-group", "", "[Required] AWS CloudWatch Log Group.")
	createAWSCloudWatchTelemetryProviderCmd.MarkFlagRequired("log-group")
	createAWSCloudWatchTelemetryProviderCmd.Flags().
		String("log-stream", "", "[Required] AWS CloudWatch Log Stream.")
	createAWSCloudWatchTelemetryProviderCmd.MarkFlagRequired("log-stream")

	createAWSCloudWatchTelemetryProviderCmd.Flags().String("role-arn", "",
		"[Optional] AWS Role ARN.")

	createAWSCloudWatchTelemetryProviderCmd.Flags().
		String("endpoint", "", "[Optional] AWS Endpoint.")

	createAWSCloudWatchTelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")

}
