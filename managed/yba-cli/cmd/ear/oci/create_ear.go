/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

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

// createOCIEARCmd represents the ear command
var createOCIEARCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere OCI encryption at rest configuration",
	Long: "Create an OCI encryption at rest configuration in YugabyteDB Anywhere. " +
		"For API_KEY auth, pass --tenancy-id, --user-id, --fingerprint, and --private-key-file-path, " +
		"or set the corresponding OCI_* environment variables. " +
		"Ensure yb.kms.allow_oci is set to true in the YugabyteDB Anywhere configuration file.",
	Example: `yba ear oci create --name <config-name> \
    --vault-id <vault-ocid> --region <region> --compartment-id <compartment-ocid> \
    --key-name <key-name> --auth-type API_KEY \
    --tenancy-id <tenancy-ocid> --user-id <user-ocid> --fingerprint <fingerprint> \
    --private-key-file-path <path-to-pem>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.CreateEARValidation(cmd)
		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authType = strings.ToUpper(authType)
		switch authType {
		case util.OCIKmsAuthTypeAPIKey:
			missing := make([]string, 0)
			for _, name := range apiKeyFlagNames {
				if util.IsEmptyString(apiKeyValueFromFlagOrEnv(cmd, name)) {
					missing = append(missing,
						fmt.Sprintf("--%s (or %s)", name, apiKeyEnvNames[name]))
				}
			}
			if len(missing) > 0 {
				cmd.Help()
				logrus.Fatalln(formatter.Colorize(
					fmt.Sprintf("Missing required values for API_KEY auth-type: %s\n",
						strings.Join(missing, ", ")),
					formatter.RedColor))
			}
		case util.OCIKmsAuthTypeInstancePrincipal:
			if apiKeyFlagsSetCount(cmd) > 0 {
				cmd.Help()
				logrus.Fatalln(formatter.Colorize(
					"API key flags cannot be set with INSTANCE_PRINCIPAL auth-type\n",
					formatter.RedColor))
			}
		default:
			cmd.Help()
			logrus.Fatalln(formatter.Colorize(
				"Invalid --auth-type. Allowed values: API_KEY, INSTANCE_PRINCIPAL\n",
				formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		runCreateOCIEAR(cmd)
	},
}

func init() {
	createOCIEARCmd.Flags().SortFlags = false

	createOCIEARCmd.Flags().String("auth-type", util.OCIKmsAuthTypeAPIKey,
		"[Optional] OCI authentication type. Allowed values: API_KEY, INSTANCE_PRINCIPAL.")
	createOCIEARCmd.Flags().String("vault-id", "",
		"[Required] OCI vault OCID.")
	createOCIEARCmd.MarkFlagRequired("vault-id")
	createOCIEARCmd.Flags().String("region", "",
		fmt.Sprintf("OCI region identifier, for example us-ashburn-1. "+
			"Can also be set using environment variable %s.", util.OCIRegionEnv))
	createOCIEARCmd.Flags().String("compartment-id", "",
		"[Required] OCI compartment OCID.")
	createOCIEARCmd.MarkFlagRequired("compartment-id")
	createOCIEARCmd.Flags().String("key-name", "",
		"[Required] Display name of the AES key in the vault. "+
			"An existing enabled AES key with this name is used. "+
			"Otherwise a new key is created.")
	createOCIEARCmd.MarkFlagRequired("key-name")

	createOCIEARCmd.Flags().String("tenancy-id", "",
		fmt.Sprintf("OCI tenancy OCID. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for API_KEY auth-type.", formatter.GreenColor),
			util.OCITenancyIDEnv))
	createOCIEARCmd.Flags().String("user-id", "",
		fmt.Sprintf("OCI user OCID. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for API_KEY auth-type.", formatter.GreenColor),
			util.OCIUserIDEnv))
	createOCIEARCmd.Flags().String("fingerprint", "",
		fmt.Sprintf("Fingerprint of the API signing key. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for API_KEY auth-type.", formatter.GreenColor),
			util.OCIFingerprintEnv))
	createOCIEARCmd.Flags().String("private-key-file-path", "",
		fmt.Sprintf("Path to the API private key PEM file. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for API_KEY auth-type.", formatter.GreenColor),
			util.OCIPrivateKeyFileEnv))
}

func runCreateOCIEAR(cmd *cobra.Command) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	configName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	authType, err := cmd.Flags().GetString("auth-type")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	authType = strings.ToUpper(authType)

	vaultID, err := cmd.Flags().GetString("vault-id")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	region, err := cmd.Flags().GetString("region")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(region) {
		region, err = util.OCIValueFromEnv(util.OCIRegionEnv)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	}
	compartmentID, err := cmd.Flags().GetString("compartment-id")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	keyName, err := cmd.Flags().GetString("key-name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	requestBody := map[string]interface{}{
		"name":                     configName,
		util.OCIAuthTypeField:      authType,
		util.OCIVaultIDField:       vaultID,
		util.OCIRegionField:        region,
		util.OCICompartmentIDField: compartmentID,
		util.OCIKeyNameField:       keyName,
	}
	if authType == util.OCIKmsAuthTypeAPIKey {
		requestBody[util.OCITenancyIDField] = apiKeyValueFromFlagOrEnv(cmd, "tenancy-id")
		requestBody[util.OCIUserIDField] = apiKeyValueFromFlagOrEnv(cmd, "user-id")
		requestBody[util.OCIFingerprintField] = apiKeyValueFromFlagOrEnv(cmd, "fingerprint")
		requestBody[util.OCIPrivateKeyContentField] = readPrivateKey(
			apiKeyValueFromFlagOrEnv(cmd, "private-key-file-path"))
	}

	rTask, response, err := authAPI.CreateKMSConfig(util.OCIEARType).
		KMSConfig(requestBody).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "EAR: OCI", "Create")
	}

	earutil.WaitForCreateEARTask(authAPI, configName, rTask, util.OCIEARType)
}

var apiKeyFlagNames = []string{
	"tenancy-id",
	"user-id",
	"fingerprint",
	"private-key-file-path",
}

var apiKeyEnvNames = map[string]string{
	"tenancy-id":            util.OCITenancyIDEnv,
	"user-id":               util.OCIUserIDEnv,
	"fingerprint":           util.OCIFingerprintEnv,
	"private-key-file-path": util.OCIPrivateKeyFileEnv,
}

// apiKeyValueFromFlagOrEnv returns the flag value, or its environment variable
// value when the flag is empty
func apiKeyValueFromFlagOrEnv(cmd *cobra.Command, name string) string {
	value, err := cmd.Flags().GetString(name)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if !util.IsEmptyString(value) {
		return value
	}
	value, err = util.OCIValueFromEnv(apiKeyEnvNames[name])
	if err != nil {
		return ""
	}
	return value
}

func apiKeyFlagsSetCount(cmd *cobra.Command) int {
	n := 0
	for _, name := range apiKeyFlagNames {
		value, err := cmd.Flags().GetString(name)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(value) {
			n++
		}
	}
	return n
}

func readPrivateKey(path string) string {
	content, err := util.ReadFileToString(path)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	return strings.TrimSpace(*content)
}
