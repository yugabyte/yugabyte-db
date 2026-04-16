/*
 * Copyright (c) YugabyteDB, Inc.
 */

package hashicorp

import (
	"fmt"

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
	Example: `yba ear hashicorp-vault create --name <config-name> \
	--token <token> --vault-address <vault-address>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.CreateEARValidation(cmd)
		token, err := cmd.Flags().GetString("token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		roleID, err := cmd.Flags().GetString("role-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(token) > 0 && len(roleID) > 0 {
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
		if util.IsEmptyString(roleID) {
			token := ""
			token, err = cmd.Flags().GetString("token")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(token) {
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
			if !util.IsEmptyString(secretID) {
				requestBody[util.HashicorpVaultSecretIDField] = secretID
			}
			if !util.IsEmptyString(namespace) {
				requestBody[util.HashicorpVaultAuthNamespaceField] = namespace
			}
		}

		address, err := cmd.Flags().GetString("vault-address")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(address) {
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
		if !util.IsEmptyString(engine) {
			requestBody[util.HashicorpVaultEngineField] = engine
		}
		keyName, err := cmd.Flags().GetString("key-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(keyName) {
			requestBody[util.HashicorpVaultKeyNameField] = keyName
		}

		mountPath, err := cmd.Flags().GetString("mount-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(mountPath) {
			requestBody[util.HashicorpVaultMountPathField] = mountPath
		}

		rTask, response, err := authAPI.CreateKMSConfig(util.HashicorpVaultEARType).
			KMSConfig(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "EAR: Hashicorp Vault", "Create")
		}

		earutil.WaitForCreateEARTask(authAPI,
			configName, rTask, util.HashicorpVaultEARType)

	},
}

func init() {
	createHashicorpVaultEARCmd.Flags().SortFlags = false

	createHashicorpVaultEARCmd.Flags().String("vault-address", "",
		fmt.Sprintf("Hashicorp Vault address. "+
			"Can also be set using environment variable %s",
			util.HashicorpVaultAddressEnv))
	createHashicorpVaultEARCmd.Flags().String("role-id", "",
		"[Optional] Hashicorp Vault AppRole ID.")
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
