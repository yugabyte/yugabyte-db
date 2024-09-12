/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateHashicorpVaultEITCmd represents the eit command
var updateHashicorpVaultEITCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere Hashicorp Vault encryption in transit configuration",
	Long:    "Update a Hashicorp Vault encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.UpdateEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		config := eitutil.GetEITConfig(
			authAPI,
			configName,
			util.HashicorpVaultCertificateType,
			"Hashicorp Vault")

		token, err := cmd.Flags().GetString("token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		hcvParams := ybaclient.HashicorpVaultConfigParams{
			VaultToken: util.GetStringPointer(token),
		}

		requestBody := ybaclient.CertificateParams{
			Label:             configName,
			CertType:          util.HashicorpVaultCertificateType,
			CertContent:       "pki",
			HcVaultCertParams: hcvParams,
		}

		eitutil.UpdateEITUtil(
			authAPI,
			configName,
			config.GetUuid(),
			util.HashicorpVaultCertificateType,
			requestBody)
	},
}

func init() {
	updateHashicorpVaultEITCmd.Flags().SortFlags = false

	updateHashicorpVaultEITCmd.Flags().String("token", "",
		"[Required] Update Hashicorp Vault Token.")
	updateHashicorpVaultEITCmd.MarkFlagRequired("token")

}
