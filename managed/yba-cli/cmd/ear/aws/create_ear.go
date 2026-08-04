/*
 * Copyright (c) YugabyteDB, Inc.
 */

package aws

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createAWSEARCmd represents the ear command
var createAWSEARCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere AWS encryption at rest configuration",
	Long:    "Create an AWS encryption at rest configuration in YugabyteDB Anywhere",
	Example: `yba ear aws create --name <config-name> \
	--access-key-id <access-key-id> --secret-access-key <secret-access-key>\
	--region <region> --cmk-id <cmk-id> --endpoint <endpoint>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.CreateEARValidation(cmd)
		isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		accessKeyID, err := cmd.Flags().GetString("access-key-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if isIAM && len(accessKeyID) > 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Cannot set both credentials and use-iam-instance-profile"+
					"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		requestBody := make(map[string]interface{})

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody["name"] = configName

		isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !isIAM {
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
			requestBody[util.AWSAccessKeyEnv] = accessKeyID
			requestBody[util.AWSSecretAccessKeyEnv] = secretAccessKey
		}

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
		requestBody[util.AWSRegionEnv] = region

		endpoint, err := cmd.Flags().GetString("endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(endpoint) {
			requestBody[util.AWSEndpointEnv] = endpoint
		}

		cmkID, err := cmd.Flags().GetString("cmk-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(cmkID) {
			requestBody[util.AWSCMKIDField] = cmkID
		} else {
			cmkPolicyFile, err := cmd.Flags().GetString("cmk-policy-file-path")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(cmkPolicyFile) {
				cmkPolicy, err := util.ReadFileToString(cmkPolicyFile)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				requestBody[util.AWSCMKPolicyField] = cmkPolicy
			}
		}

		rTask, response, err := authAPI.CreateKMSConfig(util.AWSEARType).
			KMSConfig(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "EAR: AWS", "Create")
		}

		earutil.WaitForCreateEARTask(authAPI,
			configName, rTask, util.AWSEARType)

	},
}

func init() {
	createAWSEARCmd.Flags().SortFlags = false

	createAWSEARCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("AWS Access Key ID. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for non IAM role based configurations.",
				formatter.GreenColor),
			util.AWSAccessKeyEnv))
	createAWSEARCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("AWS Secret Access Key. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for non IAM role based configurations.",
				formatter.GreenColor),
			util.AWSSecretAccessKeyEnv))
	createAWSEARCmd.MarkFlagsRequiredTogether("access-key-id", "secret-access-key")
	createAWSEARCmd.Flags().String("region", "",
		fmt.Sprintf("AWS region where the customer master key is located. "+
			"Can also be set using environment variable %s",
			util.AWSRegionEnv))
	createAWSEARCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. EAR "+
			"creation will fail on insufficient permissions on the host. (default false)")

	createAWSEARCmd.Flags().String("cmk-id", "",
		"[Optional] Customer Master Key ID. "+
			"If an identifier is not entered, a CMK ID will be auto-generated.")
	createAWSEARCmd.Flags().String("endpoint", "",
		"[Optional] AWS KMS Endpoint.")
	createAWSEARCmd.Flags().String("cmk-policy-file-path", "",
		"[Optional] AWS KMS Customer Master Key Policy file path. "+
			"Custom policy file is not needed when Customer Master Key ID is specified. "+
			"Allowed file type is json.")
}
