/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"fmt"
	"maps"
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
		"API key credentials that are not given keep their stored values. " +
		"Auth type, vault, region, and key name cannot be changed.",
	Example: `yba ear oci update --name <config-name> \
    --fingerprint <fingerprint> --private-key-file-path <path-to-pem>

yba ear oci update --name <config-name> \
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
	addUpdateOCIEARFlags(updateOCIEARCmd)
}

func addUpdateOCIEARFlags(cmd *cobra.Command) {
	cmd.Flags().String("tenancy-id", "",
		"[Optional] Update OCI tenancy OCID.")
	cmd.Flags().String("user-id", "",
		"[Optional] Update OCI user OCID.")
	cmd.Flags().String("fingerprint", "",
		"[Optional] Update API signing key fingerprint. Must be set with "+
			"--private-key-file-path.")
	cmd.Flags().String("private-key-file-path", "",
		"[Optional] Path to the updated API private key PEM file. Must be set with "+
			"--fingerprint.")
	cmd.Flags().String("compartment-id", "",
		"[Optional] Update OCI compartment OCID.")
	cmd.MarkFlagsRequiredTogether("fingerprint", "private-key-file-path")
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

	if strings.EqualFold(config.OCI.AuthType, util.OCIAuthTypeInstancePrincipal) &&
		apiKeyFlags > 0 {
		logrus.Fatalln(formatter.Colorize(
			"API key flags cannot be set on a configuration that uses "+
				"INSTANCE_PRINCIPAL auth\n",
			formatter.RedColor))
	}

	apiKeyFields, err := apiKeyUpdateFields(cmd)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(apiKeyFields) > 0 {
		hasUpdates = true
		maps.Copy(requestBody, apiKeyFields)
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

// apiKeyUpdateFields returns the auth config fields for the API key flags that
// are set. YBA keeps the stored value of every field left out of the edit.
func apiKeyUpdateFields(cmd *cobra.Command) (map[string]interface{}, error) {
	fields := map[string]interface{}{}
	for _, name := range apiKeyFlagNames {
		if !cmd.Flags().Changed(name) {
			continue
		}
		value, err := cmd.Flags().GetString(name)
		if err != nil {
			return nil, err
		}
		if util.IsEmptyString(value) {
			return nil, fmt.Errorf("--%s cannot be empty", name)
		}
		switch name {
		case "tenancy-id":
			fields[util.OCITenancyIDField] = value
		case "user-id":
			fields[util.OCIUserIDField] = value
		case "fingerprint":
			fields[util.OCIFingerprintField] = value
		case "private-key-file-path":
			content, err := util.ReadFileToString(value)
			if err != nil {
				return nil, err
			}
			fields[util.OCIPrivateKeyContentField] = strings.TrimSpace(*content)
		}
	}
	return fields, nil
}
