/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

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

// createHashicorpVaultEARCmd represents the ear command
var createHashicorpVaultEARCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere Hashicorp Vault encryption at rest configuration",
	Long:    "Create a Hashicorp Vault encryption at rest configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		configNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(configNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No encryption at rest config name found to create\n",
					formatter.RedColor))
		}
		token, err := cmd.Flags().GetString("token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		accessKeyID, err := cmd.Flags().GetString("role-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(token) > 0 && len(accessKeyID) > 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Cannot set both AppRole credentials and token"+
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

		roleID, err := cmd.Flags().GetString("role-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(roleID)) == 0 {
			token := ""
			token, err = cmd.Flags().GetString("token")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(strings.TrimSpace(token)) == 0 {
				token, err = util.HashicorpVaultTokenFromEnv()
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			// set token here
			requestBody[util.HashicorpVaultTokenField] = token
		} else {
			secretID, err := cmd.Flags().GetString("secret-id")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			namespace, err := cmd.Flags().GetString("auth-namespace")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			requestBody[util.HashicorpVaultRoleIDField] = roleID
			if len(strings.TrimSpace(secretID)) > 0 {
				requestBody[util.HashicorpVaultSecretIDField] = secretID
			}
			if len(strings.TrimSpace(namespace)) > 0 {
				requestBody[util.HashicorpVaultAuthNamespaceField] = namespace
			}
		}

		address, err := cmd.Flags().GetString("vault-address")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(address)) == 0 {
			address, err = util.HashicorpVaultAddressFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		requestBody[util.HashicorpVaultAddressField] = address

		engine, err := cmd.Flags().GetString("secret-engine")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(engine)) != 0 {
			requestBody[util.HashicorpVaultEngineField] = engine
		}
		keyName, err := cmd.Flags().GetString("key-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(keyName)) != 0 {
			requestBody[util.HashicorpVaultKeyNameField] = keyName
		}

		mountPath, err := cmd.Flags().GetString("mount-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(mountPath)) != 0 {
			requestBody[util.HashicorpVaultMountPathField] = mountPath
		}

		rCreate, response, err := authAPI.CreateKMSConfig(util.HashicorpVaultEARType).
			KMSConfig(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "EAR: Hashicorp Vault", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		configUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		earutil.WaitForCreateEARTask(authAPI,
			configName, configUUID, util.HashicorpVaultEARType, taskUUID)

	},
}

func init() {
	createHashicorpVaultEARCmd.Flags().SortFlags = false

	createHashicorpVaultEARCmd.Flags().String("vault-address", "",
		fmt.Sprintf("Hashicorp Vault address. "+
			"Can also be set using environment variable %s",
			util.HashicorpVaultAddressEnv))
	createHashicorpVaultEARCmd.Flags().String("role-id", "",
		"[Optional] Hashicorp Vault AppRole ID. ")
	createHashicorpVaultEARCmd.Flags().String("secret-id", "",
		"[Optional] Hashicorp Vault AppRole Secret ID.")
	createHashicorpVaultEARCmd.Flags().String("auth-namespace", "",
		"[Optional] Hashicorp Vault AppRole Auth Namespace.")
	createHashicorpVaultEARCmd.MarkFlagsRequiredTogether("role-id", "secret-id")
	createHashicorpVaultEARCmd.Flags().String("token", "",
		fmt.Sprintf("[Optional] Hashicorp Vault Token. "+
			"%s "+
			"Can also be set using environment variable %s",
			formatter.Colorize("Required if AppRole credentials are not provided.",
				formatter.GreenColor),
			util.HashicorpVaultTokenEnv))
	createHashicorpVaultEARCmd.Flags().String("secret-engine", "transit",
		"[Optional] Hashicorp Vault Secret Engine. Allowed values: transit.")
	createHashicorpVaultEARCmd.Flags().String("key-name", "key_yugabyte",
		"[Optional] Hashicorp Vault key name. "+
			"If key with same name already exists then it will be used,"+
			" else a new one will be created automatically.")
	createHashicorpVaultEARCmd.Flags().String("mount-path", "transit/",
		"[Optional] Hashicorp Vault mount path.")
}
