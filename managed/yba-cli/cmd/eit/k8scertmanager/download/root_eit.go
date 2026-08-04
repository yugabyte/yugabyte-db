/*
 * Copyright (c) YugabyteDB, Inc.
 */

package download

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var downloadRootK8sCertManagerEITCmd = &cobra.Command{
	Use: "root",
	Short: "Download a K8s Cert Manager YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's root certifciate",
	Long: "Download a K8s Cert Manager YugabyteDB Anywhere Encryption In Transit " +
		"(EIT) configuration's root certificate",
	Example: `yba eit k8s-cert-manager download root --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DownloadEITValidation(cmd, "root")
	},
	Run: func(cmd *cobra.Command, args []string) {

		eitutil.DownloadRootEITUtil(cmd, "K8s Cert Manager", util.K8sCertManagerCertificateType)

	},
}

func init() {
	downloadRootK8sCertManagerEITCmd.Flags().SortFlags = false
}
