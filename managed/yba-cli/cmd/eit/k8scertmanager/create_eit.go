/*
 * Copyright (c) YugaByte, Inc.
 */

package k8scertmanager

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

// createK8sCertManagerEITCmd represents the eit command
var createK8sCertManagerEITCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add", "upload"},
	Short:   "Create a YugabyteDB Anywhere K8s Cert Manager encryption in transit configuration",
	Long:    "Create a K8s Cert Manager encryption in transit configuration in YugabyteDB Anywhere",
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

		requestBody := ybaclient.CertificateParams{
			Label:       configName,
			CertType:    util.K8sCertManagerCertificateType,
			CertContent: *rootCertContent,
		}

		eitutil.CreateEITUtil(authAPI, configName, util.K8sCertManagerCertificateType, requestBody)
	},
}

func init() {
	createK8sCertManagerEITCmd.Flags().SortFlags = false

	createK8sCertManagerEITCmd.Flags().String("root-cert-file-path", "",
		"[Required] Root certificate file path.")
	createK8sCertManagerEITCmd.MarkFlagRequired("root-cert-file-path")

}
