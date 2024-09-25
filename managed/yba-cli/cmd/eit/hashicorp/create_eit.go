/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createHashicorpVaultEITCmd represents the eit command
var createHashicorpVaultEITCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add", "upload"},
	Short:   "Create a YugabyteDB Anywhere Hashicorp Vault encryption in transit configuration",
	Long:    "Create a Hashicorp Vault encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.CreateEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		token, err := cmd.Flags().GetString("token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(token)) == 0 {
			token, err = util.HashicorpVaultTokenFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
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

		engine, err := cmd.Flags().GetString("secret-engine")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		role, err := cmd.Flags().GetString("role")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		mountPath, err := cmd.Flags().GetString("mount-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		hcvParams := ybaclient.HashicorpVaultConfigParams{
			VaultToken: util.GetStringPointer(token),
			VaultAddr:  address,
			Engine:     engine,
			Role:       role,
			MountPath:  mountPath,
		}

		requestBody := ybaclient.CertificateParams{
			Label:             configName,
			CertType:          util.HashicorpVaultCertificateType,
			CertContent:       "pki",
			HcVaultCertParams: hcvParams,
		}

		eitutil.CreateEITUtil(authAPI, configName, util.HashicorpVaultCertificateType, requestBody)
	},
}

func init() {
	createHashicorpVaultEITCmd.Flags().SortFlags = false

	createHashicorpVaultEITCmd.Flags().String("vault-address", "",
		fmt.Sprintf("Hashicorp Vault address. "+
			"Can also be set using environment variable %s",
			util.HashicorpVaultAddressEnv))
	createHashicorpVaultEITCmd.Flags().String("token", "",
		fmt.Sprintf("Hashicorp Vault Token. "+
			"Can also be set using environment variable %s",
			util.HashicorpVaultTokenEnv))
	createHashicorpVaultEITCmd.Flags().String("secret-engine", "pki",
		"[Optional] Hashicorp Vault Secret Engine. Allowed values: pki.")
	createHashicorpVaultEITCmd.Flags().String("role", "",
		"[Required] The role used for creating certificates in Hashicorp Vault.")
	createHashicorpVaultEITCmd.MarkFlagRequired("role")
	createHashicorpVaultEITCmd.Flags().String("mount-path", "pki/",
		"[Optional] Hashicorp Vault mount path.")
}
