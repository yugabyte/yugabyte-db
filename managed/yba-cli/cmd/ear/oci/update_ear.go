/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateOCIEARCmd represents the ear command
var updateOCIEARCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere OCI encryption at rest (EAR) configuration",
	Long: "Update an OCI encryption at rest (EAR) configuration in YugabyteDB Anywhere. " +
		"API key credentials and compartment OCID can be updated. " +
		"Auth type, vault, region, and key name cannot be changed.",
	Example: `yba ear oci update --name <config-name> \
    --tenancy-id <tenancy-ocid> --user-id <user-ocid> --fingerprint <fingerprint> \
    --private-key-file-path <path-to-pem>

yba ear oci update --name <config-name> --compartment-id <compartment-ocid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configNameFlag) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No encryption at rest config name found to update\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		runUpdateOCIEAR(cmd)
	},
}

func init() {
	updateOCIEARCmd.Flags().SortFlags = false

	updateOCIEARCmd.Flags().String("tenancy-id", "",
		"[Optional] Update OCI tenancy OCID. Must be set with --user-id, "+
			"--fingerprint, and --private-key-file-path.")
	updateOCIEARCmd.Flags().String("user-id", "",
		"[Optional] Update OCI user OCID. Must be set with --tenancy-id, "+
			"--fingerprint, and --private-key-file-path.")
	updateOCIEARCmd.Flags().String("fingerprint", "",
		"[Optional] Update API signing key fingerprint. Must be set with --tenancy-id, "+
			"--user-id, and --private-key-file-path.")
	updateOCIEARCmd.Flags().String("private-key-file-path", "",
		"[Optional] Path to the updated API private key PEM file. Must be set with "+
			"--tenancy-id, --user-id, and --fingerprint.")
	updateOCIEARCmd.Flags().String("compartment-id", "",
		"[Optional] Update OCI compartment OCID.")
	updateOCIEARCmd.MarkFlagsRequiredTogether(apiKeyFlagNames...)
}

func runUpdateOCIEAR(cmd *cobra.Command) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	configName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	config, err := earutil.GetEARConfig(authAPI, configName, util.OCIEARType)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if config.OCI == nil {
		logrus.Fatalf("No OCI details found for %s\n", configName)
	}

	requestBody := map[string]interface{}{}
	hasUpdates := false
	apiKeyFlags := apiKeyFlagsSetCount(cmd)

	if strings.EqualFold(config.OCI.AuthType, util.OCIKmsAuthTypeInstancePrincipal) &&
		apiKeyFlags > 0 {
		logrus.Fatalln(formatter.Colorize(
			"API key flags cannot be set on a configuration that uses "+
				"INSTANCE_PRINCIPAL auth\n",
			formatter.RedColor))
	}

	if apiKeyFlags == len(apiKeyFlagNames) {
		hasUpdates = true
		tenancyID, err := cmd.Flags().GetString("tenancy-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		userID, err := cmd.Flags().GetString("user-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		fingerprint, err := cmd.Flags().GetString("fingerprint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody[util.OCITenancyIDField] = tenancyID
		requestBody[util.OCIUserIDField] = userID
		privateKeyFilePath, err := cmd.Flags().GetString("private-key-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody[util.OCIFingerprintField] = fingerprint
		requestBody[util.OCIPrivateKeyContentField] = readPrivateKey(privateKeyFilePath)
	}

	if cmd.Flags().Changed("compartment-id") {
		compartmentID, err := cmd.Flags().GetString("compartment-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(compartmentID) {
			logrus.Fatalln(formatter.Colorize(
				"--compartment-id cannot be empty\n", formatter.RedColor))
		}
		hasUpdates = true
		requestBody[util.OCICompartmentIDField] = compartmentID
	}

	if hasUpdates {
		earutil.UpdateEARConfig(
			authAPI,
			configName,
			config.ConfigUUID,
			util.OCIEARType,
			requestBody)
		return
	}
	logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))
}
