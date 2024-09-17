/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateAWSEARCmd represents the ear command
var updateAWSEARCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere AWS encryption at rest (EAR) configuration",
	Long:    "Update an AWS encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		configNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(configNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No encryption at rest config name found to update\n",
					formatter.RedColor))
		}
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

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		config, err := earutil.GetEARConfig(authAPI, configName, util.AWSEARType)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		aws := config.AWS
		if aws == nil {
			logrus.Fatalf("No AWS details found for %s\n", configName)
		}

		requestBody := make(map[string]interface{})

		hasUpdates := false

		accessKeyID, err := cmd.Flags().GetString("access-key-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(accessKeyID)) != 0 && len(strings.TrimSpace(secretAccessKey)) != 0 {
			logrus.Debug("Updating AWS credentials\n")
			hasUpdates = true
			requestBody[util.AWSAccessKeyEnv] = accessKeyID
			requestBody[util.AWSSecretAccessKeyEnv] = secretAccessKey
		}

		isIAMFlagSet := cmd.Flags().Changed("use-iam-instance-profile")
		if isIAMFlagSet {
			isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if isIAM {
				logrus.Debug("Updating configuration to use IAM instance profile\n")
				hasUpdates = true
				requestBody[util.AWSAccessKeyEnv] = ""
				requestBody[util.AWSSecretAccessKeyEnv] = ""
			}
		}

		endpoint, err := cmd.Flags().GetString("endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(endpoint)) != 0 && strings.Compare(aws.EndPoint, endpoint) != 0 {
			logrus.Debug("Updating AWS endpoint\n")
			hasUpdates = true
			requestBody[util.AWSEndpointEnv] = endpoint
		}

		if hasUpdates {
			earutil.UpdateEARConfig(
				authAPI,
				configName,
				config.ConfigUUID,
				util.AWSEARType,
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))

	},
}

func init() {
	updateAWSEARCmd.Flags().SortFlags = false

	updateAWSEARCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("[Optional] Update AWS Access Key ID. %s",
			formatter.Colorize("Required for non IAM role based configurations.",
				formatter.GreenColor)))
	updateAWSEARCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("[Optional] Update AWS Secret Access Key. %s",
			formatter.Colorize("Required for non IAM role based configurations.",
				formatter.GreenColor)))
	updateAWSEARCmd.MarkFlagsRequiredTogether("access-key-id", "secret-access-key")
	updateAWSEARCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. EAR "+
			"creation will fail on insufficient permissions on the host. (default false)")
	updateAWSEARCmd.Flags().String("endpoint", "",
		"[Optional] Updating AWS KMS Endpoint.")

}
