/*
 * Copyright (c) YugaByte, Inc.
 */

package security

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/upgrade"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// encryptionAtRestCmd represents the universe security encryption-at-rest command
var encryptionAtRestCmd = &cobra.Command{
	Use:     "ear",
	Aliases: []string{"encryption-at-rest", "kms"},
	Short:   "Encryption-at-rest settings for a universe",
	Long:    "Encryption-at-rest settings for a universe",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to change settings\n",
					formatter.RedColor))
		}

		// Validations before gflags upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := upgrade.Validations(cmd, util.SecurityOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}

		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to change encryption at rest configuration for %s: %s",
				util.UniverseType, universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := upgrade.Validations(cmd, util.UpgradeOperation)
		if err != nil {
			logrus.Fatalf(
				formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()
		universeDetails := universe.GetUniverseDetails()
		earConfig := universeDetails.GetEncryptionAtRestConfig()

		operation, err := cmd.Flags().GetString("operation")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		configName, err := cmd.Flags().GetString("config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		operation = strings.ToUpper(operation)
		if strings.Compare(operation, util.EnableKMSOpType) != 0 &&
			strings.Compare(operation, util.DisableKMSOpType) != 0 &&
			strings.Compare(operation, util.RotateKMSConfigKMSOpType) != 0 &&
			strings.Compare(operation, util.RotateUniverseKeyKMSOpType) != 0 {
			logrus.Fatalf(
				formatter.Colorize(
					"Invalid operation. "+
						"Allowed values: enable, disable,"+
						" rotate-universe-key, rotate-kms-config\n", formatter.RedColor))
		}
		var requestBody ybaclient.EncryptionAtRestConfig
		switch operation {
		case util.EnableKMSOpType, util.RotateKMSConfigKMSOpType:
			requestBody, err = earEnableRequest(authAPI, earConfig, configName, operation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		case util.DisableKMSOpType:
			requestBody, err = earDisableRequest(earConfig)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		case util.RotateUniverseKeyKMSOpType:
			if earConfig.GetEncryptionAtRestEnabled() ||
				strings.Compare(earConfig.GetOpType(), util.EnableKMSOpType) == 0 {
				requestBody = ybaclient.EncryptionAtRestConfig{
					KmsConfigUUID: util.GetStringPointer(earConfig.GetKmsConfigUUID()),
					OpType:        util.GetStringPointer(util.EnableKMSOpType),
				}
			} else {
				logrus.Fatal(
					formatter.Colorize(
						"Cannot rotate universe key if encryption at rest is not enabled",
						formatter.RedColor))
			}

		}

		earAPICall(authAPI, universeName, universeUUID, requestBody)

	},
}

func init() {
	encryptionAtRestCmd.Flags().SortFlags = false

	encryptionAtRestCmd.Flags().String("operation", "",
		"[Required] Enable or disable encryption-at-rest in a universe. "+
			"Allowed values: enable, disable, rotate-universe-key, rotate-kms-config.")
	encryptionAtRestCmd.MarkFlagRequired("operation")
	encryptionAtRestCmd.Flags().String("config-name", "",
		fmt.Sprintf("[Optional] Key management service configuration name for master key. %s.",
			formatter.Colorize("Required for enable and rotate-kms-config operations, "+
				"ignored otherwise", formatter.GreenColor)))

}

func earEnableRequest(
	authAPI *client.AuthAPIClient,
	earConfig ybaclient.EncryptionAtRestConfig,
	configName, operation string) (
	ybaclient.EncryptionAtRestConfig,
	error,
) {
	if earConfig.GetEncryptionAtRestEnabled() ||
		strings.Compare(earConfig.GetOpType(), util.EnableKMSOpType) == 0 {
		if strings.Compare(operation, util.EnableKMSOpType) == 0 {
			logrus.Fatalf(
				formatter.Colorize("Encryption at rest is already enabled\n",
					formatter.RedColor))
		}
	} else {
		if strings.Compare(operation, util.RotateKMSConfigKMSOpType) == 0 {
			logrus.Fatalf(
				formatter.Colorize("Encryption at rest is not enabled, cannot rotate KMS configuration\n",
					formatter.RedColor))
		}
	}

	if len(strings.TrimSpace(configName)) == 0 {
		logrus.Fatalf(
			formatter.Colorize(
				"Configuration name is required to enable encryption at rest "+
					"or to rotate KMS configuration.\n",
				formatter.RedColor))
	}

	configUUID, err := getKMSConfigUUID(authAPI, configName)
	if err != nil {
		logrus.Fatalf(
			formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	if strings.Compare(operation, util.RotateKMSConfigKMSOpType) == 0 &&
		strings.Compare(configUUID, earConfig.GetKmsConfigUUID()) == 0 {
		logrus.Fatal(
			formatter.Colorize(
				fmt.Sprintf("Universe encryption at rest is already using configuration: %s (%s)\n",
					configName, configUUID),
				formatter.RedColor,
			),
		)
	}
	{
	}

	return ybaclient.EncryptionAtRestConfig{
		KmsConfigUUID: util.GetStringPointer(configUUID),
		OpType:        util.GetStringPointer(util.EnableKMSOpType),
	}, nil

}

func earDisableRequest(
	earConfig ybaclient.EncryptionAtRestConfig) (
	ybaclient.EncryptionAtRestConfig,
	error,
) {
	if !earConfig.GetEncryptionAtRestEnabled() ||
		strings.Compare(earConfig.GetOpType(), util.DisableKMSOpType) == 0 {
		logrus.Fatalf(
			formatter.Colorize("Encryption at rest is already disabled\n",
				formatter.RedColor))
	}

	return ybaclient.EncryptionAtRestConfig{
		OpType: util.GetStringPointer(util.DisableKMSOpType),
	}, nil
}

func getKMSConfigUUID(authAPI *client.AuthAPIClient, configName string) (string, error) {
	r, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Universe", "Master Key Rotation - List")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	for _, k := range r {
		kmsConfig, err := util.ConvertToKMSConfig(k)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if strings.Compare(kmsConfig.Name, configName) == 0 {
			return kmsConfig.ConfigUUID, nil
		}

	}
	return "", fmt.Errorf("No configurations with name: %s found\n", configName)
}

func earAPICall(
	authAPI *client.AuthAPIClient,
	universeName string,
	universeUUID string,
	requestBody ybaclient.EncryptionAtRestConfig,
) {
	r, response, err := authAPI.SetUniverseKey(
		universeUUID).SetUniverseKeyRequest(requestBody).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Universe", "Master Key Rotation")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	taskUUID := r.GetTaskUUID()
	logrus.Info(
		fmt.Sprintf("Setting encryption at rest configuration in universe %s (%s)\n",
			formatter.Colorize(universeName, formatter.GreenColor),
			universeUUID,
		))

	upgrade.WaitForUpgradeUniverseTask(authAPI, universeName, universeUUID, taskUUID)

}
