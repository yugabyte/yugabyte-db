/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

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

// createGCPEARCmd represents the ear command
var createGCPEARCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere GCP encryption at rest configuration",
	Long:    "Create a GCP encryption at rest configuration in YugabyteDB Anywhere",
	Example: `yba ear gcp create --name <config-name> \
	--credentials-file-path <credentials-file-path> \
	--key-ring-name <key-ring-name> --crypto-key-name <crypto-key-name> \
	--protection-level <protection-level>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.CreateEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		requestBody := make(map[string]interface{})

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody["name"] = configName

		gcsFilePath, err := cmd.Flags().GetString("credentials-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var gcsCreds map[string]interface{}
		if len(gcsFilePath) == 0 {
			gcsCreds, err = util.GcpGetCredentialsAsMap()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

		} else {
			gcsCreds, err = util.GcpGetCredentialsAsMapFromFilePath(gcsFilePath)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		requestBody[util.GCPConfigField] = gcsCreds

		location, err := cmd.Flags().GetString("location")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(location) {
			requestBody[util.GCPLocationIDField] = location
		}

		endpoint, err := cmd.Flags().GetString("endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(endpoint) {
			requestBody[util.GCPKmsEndpointField] = endpoint
		}

		keyRingName, err := cmd.Flags().GetString("key-ring-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(keyRingName) {
			requestBody[util.GCPKeyRingIDField] = keyRingName
		}

		cryptoKeyName, err := cmd.Flags().GetString("crypto-key-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(cryptoKeyName) {
			requestBody[util.GCPCryptoKeyIDField] = cryptoKeyName
		}

		protectionLevel, err := cmd.Flags().GetString("protection-level")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(protectionLevel) {
			requestBody[util.GCPProtectionLevelField] = strings.ToUpper(protectionLevel)
		}

		rTask, response, err := authAPI.CreateKMSConfig(util.GCPEARType).
			KMSConfig(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "EAR: GCP", "Create")
		}

		earutil.WaitForCreateEARTask(authAPI,
			configName, rTask, util.GCPEARType)

	},
}

func init() {
	createGCPEARCmd.Flags().SortFlags = false

	createGCPEARCmd.Flags().String("credentials-file-path", "",
		fmt.Sprintf("GCP Credentials File Path. "+
			"Can also be set using environment variable %s.",
			util.GCPCredentialsEnv))

	createGCPEARCmd.Flags().String("location", "global",
		"[Optional] The geographical region where the Cloud KMS resource is stored and accessed.")
	createGCPEARCmd.Flags().String("key-ring-name", "",
		"[Required] Name of the key ring. "+
			"If key ring with same name already exists then it will be used, "+
			"else a new one will be created automatically.")
	createGCPEARCmd.Flags().String("crypto-key-name", "",
		"[Required] Name of the cryptographic key that will be used "+
			"for encrypting and decrypting universe key. "+
			"If crypto key with same name already exists then it will be used, "+
			"else a new one will be created automatically.")
	createGCPEARCmd.MarkFlagRequired("crypto-key-name")
	createGCPEARCmd.MarkFlagRequired("key-ring-name")
	createGCPEARCmd.Flags().String("protection-level", "hsm",
		"[Optional] The protection level to use for this key. "+
			"Allowed values: software, hsm.")
	createGCPEARCmd.Flags().String("endpoint", "",
		"[Optional] GCP KMS Endpoint.")

}
