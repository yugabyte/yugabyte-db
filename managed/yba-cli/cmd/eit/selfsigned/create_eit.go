/*
 * Copyright (c) YugaByte, Inc.
 */

package selfsigned

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createSelfSignedEITCmd represents the eit command
var createSelfSignedEITCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add", "upload"},
	Short:   "Create a YugabyteDB Anywhere Self Signed encryption in transit configuration",
	Long:    "Create a Self Signed encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.CreateEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rootCertFilePath, err := cmd.Flags().GetString("root-cert-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(rootCertFilePath)) == 0 {
			logrus.Fatalf(formatter.Colorize("Missing root certificate file path\n", formatter.RedColor))
		}

		logrus.Debug("Reading contents from root certificate file: ", rootCertFilePath)
		rootCertContent, err := util.ReadFileToString(rootCertFilePath)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		keyFilePath, err := cmd.Flags().GetString("key-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(keyFilePath)) == 0 {
			logrus.Fatalf(formatter.Colorize("Missing key file path\n", formatter.RedColor))
		}

		logrus.Debug("Reading contents from key file: ", keyFilePath)
		keyContent, err := util.ReadFileToString(keyFilePath)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.CertificateParams{
			Label:       configName,
			CertType:    util.SelfSignedCertificateType,
			CertContent: *rootCertContent,
			KeyContent:  *keyContent,
		}

		eitutil.CreateEITUtil(authAPI, configName, util.SelfSignedCertificateType, requestBody)
	},
}

func init() {
	createSelfSignedEITCmd.Flags().SortFlags = false

	createSelfSignedEITCmd.Flags().String("root-cert-file-path", "",
		"[Required] Root certificate file path.")
	createSelfSignedEITCmd.MarkFlagRequired("root-cert-file-path")
	createSelfSignedEITCmd.Flags().String("key-file-path", "",
		"[Required] Private key file path.")
	createSelfSignedEITCmd.MarkFlagRequired("key-file-path")
}
