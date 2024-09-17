/*
 * Copyright (c) YugaByte, Inc.
 */

package customca

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

// createCustomCAEITCmd represents the eit command
var createCustomCAEITCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add", "upload"},
	Short:   "Create a YugabyteDB Anywhere Custom CA encryption in transit configuration",
	Long:    "Create a Custom CA encryption in transit configuration in YugabyteDB Anywhere",
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

		rootCAFilePathOnNode, err := cmd.Flags().GetString("root-ca-file-path-on-node")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		nodeCertFilePathOnNode, err := cmd.Flags().GetString("node-cert-file-path-on-node")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		nodeKeyFilePathOnNode, err := cmd.Flags().GetString("node-key-file-path-on-node")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(rootCAFilePathOnNode)) == 0 {
			logrus.Fatalf(formatter.Colorize("Missing root CA certificate file path on node\n",
				formatter.RedColor))
		}

		if len(strings.TrimSpace(nodeCertFilePathOnNode)) == 0 {
			logrus.Fatalf(formatter.Colorize("Missing node certificate file path on node\n",
				formatter.RedColor))
		}

		if len(strings.TrimSpace(nodeKeyFilePathOnNode)) == 0 {
			logrus.Fatalf(formatter.Colorize("Missing node key file path on node\n",
				formatter.RedColor))
		}

		clientCertFilePathOnNode, err := cmd.Flags().GetString("client-cert-file-path-on-node")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		clientKeyFilePathOnNode, err := cmd.Flags().GetString("client-key-file-path-on-node")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		customCertInfo := ybaclient.CustomCertInfo{
			RootCertPath:   rootCAFilePathOnNode,
			NodeCertPath:   nodeCertFilePathOnNode,
			NodeKeyPath:    nodeKeyFilePathOnNode,
			ClientCertPath: clientCertFilePathOnNode,
			ClientKeyPath:  clientKeyFilePathOnNode,
		}

		requestBody := ybaclient.CertificateParams{
			Label:          configName,
			CertType:       util.CustomCertHostPathCertificateType,
			CertContent:    *rootCertContent,
			CustomCertInfo: customCertInfo,
		}

		eitutil.CreateEITUtil(authAPI, configName, util.CustomCertHostPathCertificateType, requestBody)
	},
}

func init() {
	createCustomCAEITCmd.Flags().SortFlags = false

	createCustomCAEITCmd.Flags().String("root-cert-file-path", "",
		"[Required] Root certificate file path to upload.")
	createCustomCAEITCmd.MarkFlagRequired("root-cert-file-path")
	createCustomCAEITCmd.Flags().String("root-ca-file-path-on-node", "",
		"[Required] Root CA certificate file path on the on-premises node.")
	createCustomCAEITCmd.MarkFlagRequired("root-ca-file-path-on-node")
	createCustomCAEITCmd.Flags().String("node-cert-file-path-on-node", "",
		"[Required] Node certificate file path on the on-premises node.")
	createCustomCAEITCmd.MarkFlagRequired("node-cert-file-path-on-node")
	createCustomCAEITCmd.Flags().String("node-key-file-path-on-node", "",
		"[Required] Node key file path on the on-premises node.")
	createCustomCAEITCmd.MarkFlagRequired("node-key-file-path-on-node")
	createCustomCAEITCmd.Flags().String("client-cert-file-path-on-node", "",
		"[Optional] Client certificate file path on the on-premises "+
			"node to enable client-to-node TLS.")
	createCustomCAEITCmd.Flags().String("client-key-file-path-on-node", "",
		"[Optional] Client key file path on the on-premises "+
			"node to enable client-to-node TLS.")
}
