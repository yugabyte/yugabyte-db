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

// updateHashicorpVaultEARCmd represents the ear command
var updateHashicorpVaultEARCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere Hashicorp Vault encryption at rest (EAR) configuration",
	Long:    "Update a Hashicorp Vault encryption at rest (EAR) configuration in YugabyteDB Anywhere",
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

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		config, err := earutil.GetEARConfig(authAPI, configName, util.HashicorpVaultEARType)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		hashicorp := config.Hashicorp
		if hashicorp == nil {
			logrus.Fatalf("No Hashicorp Vault details found for %s\n", configName)
		}

		requestBody := make(map[string]interface{})

		requestBody[util.HashicorpVaultAddressField] = hashicorp.HcVaultAddress

		hasUpdates := false

		roleID, err := cmd.Flags().GetString("role-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		token, err := cmd.Flags().GetString("token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(roleID)) != 0 {
			logrus.Debug("Updating Hashicorp Vault role ID\n")
			hasUpdates = true
			requestBody[util.HashicorpVaultRoleIDField] = roleID
		} else if len(strings.TrimSpace(token)) != 0 {
			logrus.Debug("Updating Hashicorp Vault token\n")
			hasUpdates = true
			requestBody[util.HashicorpVaultTokenField] = token
		}

		if (requestBody[util.HashicorpVaultRoleIDField] != nil &&
			len(strings.TrimSpace(requestBody[util.HashicorpVaultRoleIDField].(string))) != 0) ||
			hashicorp.HcVaultRoleID != "" {
			namespace, err := cmd.Flags().GetString("auth-namespace")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(strings.TrimSpace(namespace)) != 0 {
				logrus.Debug("Updating Hashicorp Vault auth namespace\n")
				hasUpdates = true
				requestBody[util.HashicorpVaultAuthNamespaceField] = namespace
			}
			secretID, err := cmd.Flags().GetString("secret-id")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(strings.TrimSpace(secretID)) != 0 {
				logrus.Debug("Updating Hashicorp Vault secret ID\n")
				hasUpdates = true
				requestBody[util.HashicorpVaultSecretIDField] = secretID
			}
		}

		if hasUpdates {
			earutil.UpdateEARConfig(
				authAPI,
				configName,
				config.ConfigUUID,
				util.HashicorpVaultEARType,
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))

	},
}

func init() {
	updateHashicorpVaultEARCmd.Flags().SortFlags = false

	updateHashicorpVaultEARCmd.Flags().String("role-id", "",
		"[Optional] Update Hashicorp Vault AppRole ID.")
	updateHashicorpVaultEARCmd.Flags().String("secret-id", "",
		"[Optional] Update Hashicorp Vault AppRole Secret ID.")
	updateHashicorpVaultEARCmd.Flags().String("auth-namespace", "",
		"[Optional] Update Hashicorp Vault AppRole Auth Namespace.")
	updateHashicorpVaultEARCmd.Flags().String("token", "",
		fmt.Sprintf("[Optional] Update Hashicorp Vault Token. "+
			"%s.",
			formatter.Colorize("Required if AppRole credentials are not provided",
				formatter.GreenColor)))

}
