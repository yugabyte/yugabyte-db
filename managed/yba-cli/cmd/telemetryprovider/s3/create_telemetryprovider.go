/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createS3TelemetryProviderCmd represents the telemetryprovider command
var createS3TelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere S3 telemetry provider",
	Long:    "Create an S3 telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider s3 create --name <name> \
     --bucket <bucket> --region <region>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.S3TelemetryProviderType),
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

		bucket, err := cmd.Flags().GetString("bucket")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(bucket) {
			logrus.Fatalf(formatter.Colorize("S3 bucket is required\n", formatter.RedColor))
		}
		config.SetBucket(bucket)

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

		directoryPrefix, err := cmd.Flags().GetString("directory-prefix")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(directoryPrefix) {
			config.SetDirectoryPrefix(directoryPrefix)
		}

		filePrefix, err := cmd.Flags().GetString("file-prefix")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(filePrefix) {
			config.SetFilePrefix(filePrefix)
		}

		partition, err := cmd.Flags().GetString("partition")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(partition) {
			switch strings.ToLower(partition) {
			case util.HourS3Partition:
				config.SetPartition(util.HourS3Partition)
			case util.MinuteS3Partition:
				config.SetPartition(util.MinuteS3Partition)
			default:
				logrus.Fatalf(formatter.Colorize("Invalid partition specified. "+
					"Allowed values: hour, minute\n", formatter.RedColor))
			}
		}

		marshaler, err := cmd.Flags().GetString("marshaler")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(marshaler) {
			switch strings.ToUpper(marshaler) {
			case util.OTLPJSONMarshaler:
				config.SetMarshaler(util.OTLPJSONMarshaler)
			case util.SumoICMarshaler:
				config.SetMarshaler(util.SumoICMarshaler)
			default:
				logrus.Fatalf(formatter.Colorize("Invalid marshaler specified. "+
					"Allowed values: OTLP_JSON, SUMO_IC\n", formatter.RedColor))
			}
		}

		roleArn, err := cmd.Flags().GetString("role-arn")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(roleArn) {
			config.SetRoleArn(roleArn)
		}

		endpoint, err := cmd.Flags().GetString("endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(endpoint) {
			config.SetEndpoint(endpoint)
		}

		if cmd.Flags().Changed("disable-ssl") {
			disableSSL, err := cmd.Flags().GetBool("disable-ssl")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			config.SetDisableSSL(disableSSL)
		}

		if cmd.Flags().Changed("force-path-style") {
			forcePathStyle, err := cmd.Flags().GetBool("force-path-style")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			config.SetForcePathStyle(forcePathStyle)
		}

		if cmd.Flags().Changed("include-universe-and-node-in-prefix") {
			includeUniverseAndNode, err := cmd.Flags().
				GetBool("include-universe-and-node-in-prefix")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			config.SetIncludeUniverseAndNodeInPrefix(includeUniverseAndNode)
		}

		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.S3TelemetryProviderType, requestBody)

	},
}

func init() {
	createS3TelemetryProviderCmd.Flags().SortFlags = false

	createS3TelemetryProviderCmd.Flags().String("bucket", "", "[Required] S3 bucket name.")
	createS3TelemetryProviderCmd.MarkFlagRequired("bucket")
	createS3TelemetryProviderCmd.Flags().String("region", "",
		fmt.Sprintf("[Optional] S3 bucket region. "+
			"Can also be set using the environment variable %s.",
			util.AWSRegionEnv))
	createS3TelemetryProviderCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("[Optional] AWS Access Key ID. %s",
			formatter.Colorize(
				"Required with secret-access-key, or set both using environment variables "+
					util.AWSAccessKeyEnv+" and "+util.AWSSecretAccessKeyEnv+".",
				formatter.GreenColor)))
	createS3TelemetryProviderCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("[Optional] AWS Secret Access Key. %s",
			formatter.Colorize(
				"Required with access-key-id, or set both using environment variables "+
					util.AWSAccessKeyEnv+" and "+util.AWSSecretAccessKeyEnv+".",
				formatter.GreenColor)))
	createS3TelemetryProviderCmd.MarkFlagsRequiredTogether(
		"access-key-id", "secret-access-key")
	createS3TelemetryProviderCmd.Flags().String("role-arn", "",
		"[Optional] AWS IAM role ARN to assume when writing to the bucket.")
	createS3TelemetryProviderCmd.Flags().String("directory-prefix", "",
		"[Optional] Root directory inside the bucket. Defaults to \"yb-logs/\".")
	createS3TelemetryProviderCmd.Flags().String("file-prefix", "",
		"[Optional] Prefix for exported object names. Defaults to \"yb-otel-\".")
	createS3TelemetryProviderCmd.Flags().String("partition", "",
		"[Optional] Partition granularity for the object key layout, defaults to minute. "+
			"Allowed values (case insensitive): hour, minute.")
	createS3TelemetryProviderCmd.Flags().String("marshaler", "",
		"[Optional] Encoding of exported objects, defaults to OTLP_JSON. "+
			"Allowed values (case insensitive): OTLP_JSON, SUMO_IC. "+
			"SUMO_IC is allowed for logs only.")
	createS3TelemetryProviderCmd.Flags().String("endpoint", "",
		"[Optional] Override the endpoint instead of deriving it from region and bucket. "+
			"Use for S3 compatible object stores.")
	createS3TelemetryProviderCmd.Flags().Bool("disable-ssl", false,
		"[Optional] Disable SSL when connecting to the endpoint.")
	createS3TelemetryProviderCmd.Flags().Bool("force-path-style", false,
		"[Optional] Force path style addressing instead of virtual hosted style.")
	createS3TelemetryProviderCmd.Flags().Bool("include-universe-and-node-in-prefix", false,
		"[Optional] Append universe UUID and node name to the directory prefix.")
	createS3TelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")
}
