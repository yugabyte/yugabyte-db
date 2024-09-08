/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

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

// createAzureEARCmd represents the ear command
var createAzureEARCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere Azure encryption at rest configuration",
	Long:    "Create an Azure encryption at rest configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.CreateEARValidation(cmd)
		isIAM, err := cmd.Flags().GetBool("use-managed-identity")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		clientSecret, err := cmd.Flags().GetString("client-secret")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if isIAM && len(clientSecret) > 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Cannot set both client-secret and use-managed-identity"+
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

		clientID, err := cmd.Flags().GetString("client-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(clientID)) == 0 {
			clientID, _, err = util.AzureClientIDFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

		}
		requestBody[util.AzureClientIDField] = clientID

		tenantID, err := cmd.Flags().GetString("tenant-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(tenantID)) == 0 {
			tenantID, _, err = util.AzureTenantIDFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

		}
		requestBody[util.AzureTenantIDField] = tenantID

		isIAM, err := cmd.Flags().GetBool("use-managed-identity")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !isIAM {

			clientSecret, err := cmd.Flags().GetString("client-secret")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(clientSecret) == 0 {
				clientSecret, _, err = util.AzureClientSecretFromEnv()
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}

			requestBody[util.AzureClientSecretField] = clientSecret

		}

		vaultURL, err := cmd.Flags().GetString("vault-url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(vaultURL)) != 0 {
			requestBody[util.AzureVaultURLField] = vaultURL
		}

		keyName, err := cmd.Flags().GetString("key-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(keyName)) != 0 {
			requestBody[util.AzureKeyNameField] = keyName
		}

		keyAlgorithm, err := cmd.Flags().GetString("key-algorithm")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(keyAlgorithm)) != 0 {
			requestBody[util.AzureKeyAlgorithmField] = keyAlgorithm
		}

		if cmd.Flags().Changed("key-size") {
			keySize, err := cmd.Flags().GetInt("key-size")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.AzureCheckKeySizeForAlgo(keySize, keyAlgorithm) {
				requestBody[util.AzureKeySizeField] = keySize
			} else {
				logrus.Fatalf(formatter.Colorize("Invalid key size for algorithm\n", formatter.RedColor))
			}
		} else {
			keySize := util.AzureGetDefaultKeySizeForAlgo(keyAlgorithm)
			if keySize != 0 {
				requestBody[util.AzureKeySizeField] = keySize
			} else {
				logrus.Fatalf(formatter.Colorize("Invalid key size for algorithm\n", formatter.RedColor))
			}
		}

		rCreate, response, err := authAPI.CreateKMSConfig(util.AzureEARType).
			KMSConfig(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "EAR: Azure", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		configUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		earutil.WaitForCreateEARTask(authAPI,
			configName, configUUID, util.AzureEARType, taskUUID)

	},
}

func init() {
	createAzureEARCmd.Flags().SortFlags = false

	createAzureEARCmd.Flags().String("client-id", "",
		fmt.Sprintf("Azure Client ID. "+
			"Can also be set using environment variable %s.", util.AzureClientIDEnv))

	createAzureEARCmd.Flags().String("tenant-id", "",
		fmt.Sprintf("Azure Tenant ID. "+
			"Can also be set using environment variable %s.", util.AzureTenantIDEnv))

	createAzureEARCmd.Flags().String("client-secret", "",
		fmt.Sprintf("Azure Secret Access Key. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for Non Managed Identity based configurations.",
				formatter.GreenColor),
			util.AzureClientSecretEnv))
	createAzureEARCmd.Flags().Bool("use-managed-identity", false,
		"[Optional] Use Azure Managed Identity from the YugabyteDB Anywhere Host. EAR "+
			"creation will fail on insufficient permissions on the host. (default false)")

	createAzureEARCmd.Flags().String("vault-url", "", "[Required] Azure Vault URL.")
	createAzureEARCmd.MarkFlagRequired("vault-url")
	createAzureEARCmd.Flags().String("key-name", "",
		"[Required] Azure Key Name."+
			"If master key with same name already exists then it will be used,"+
			" else a new one will be created automatically.")
	createAzureEARCmd.MarkFlagRequired("key-name")
	createAzureEARCmd.Flags().String("key-algorithm", "RSA",
		"[Optional] Azure Key Algorithm. Allowed values (case sensitive): RSA")
	createAzureEARCmd.Flags().Int("key-size", 0,
		"[Optional] Azure Key Size. Allowed values per algorithm: RSA(Default:2048, 3072, 4096)")
}
