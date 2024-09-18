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

// updateAzureEARCmd represents the ear command
var updateAzureEARCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere Azure encryption at rest (EAR) configuration",
	Long:    "Update an Azure encryption at rest (EAR) configuration in YugabyteDB Anywhere",
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

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		config, err := earutil.GetEARConfig(authAPI, configName, util.AzureEARType)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		azure := config.Azure
		if azure == nil {
			logrus.Fatalf("No Azure details found for %s\n", configName)
		}

		requestBody := make(map[string]interface{})

		hasUpdates := false

		clientID, err := cmd.Flags().GetString("client-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(clientID)) != 0 {
			logrus.Debug("Updating Azure Client ID\n")
			hasUpdates = true
			requestBody[util.AzureClientIDField] = clientID
		}

		tenantID, err := cmd.Flags().GetString("tenant-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(tenantID)) != 0 {
			logrus.Debug("Updating Azure Tenant ID\n")
			hasUpdates = true
			requestBody[util.AzureTenantIDField] = tenantID
		}

		clientSecret, err := cmd.Flags().GetString("client-secret")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(clientSecret)) != 0 {
			logrus.Debug("Updating Azure Client Secret\n")
			hasUpdates = true
			requestBody[util.AzureClientSecretField] = clientSecret
		}

		isIAMFlagSet := cmd.Flags().Changed("use-managed-identity")
		if isIAMFlagSet {
			isIAM, err := cmd.Flags().GetBool("use-managed-identity")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if isIAM {
				logrus.Debug("Updating configuration to use Managed Identity\n")
				hasUpdates = true
				requestBody[util.AzureClientSecretField] = ""
			}
		}

		if hasUpdates {
			earutil.UpdateEARConfig(
				authAPI,
				configName,
				config.ConfigUUID,
				util.AzureEARType,
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))

	},
}

func init() {
	updateAzureEARCmd.Flags().SortFlags = false

	updateAzureEARCmd.Flags().String("client-id", "",
		"[Optional] Update Azure Client ID.")
	updateAzureEARCmd.Flags().String("tenant-id", "",
		"[Optional] Update Azure Tenant ID.")
	updateAzureEARCmd.Flags().String("client-secret", "",
		fmt.Sprintf("[Optional] Update Azure Secret Access Key. %s ",
			formatter.Colorize("Required for Non Managed Identity based configurations.",
				formatter.GreenColor)))
	updateAzureEARCmd.Flags().Bool("use-managed-identity", false,
		"[Optional] Use Azure Managed Identity from the YugabyteDB Anywhere Host. EAR "+
			"creation will fail on insufficient permissions on the host. (default false)")

}
