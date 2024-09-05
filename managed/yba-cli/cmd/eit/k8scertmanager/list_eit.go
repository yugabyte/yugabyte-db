/*
 * Copyright (c) YugaByte, Inc.
 */

package k8scertmanager

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listK8sCertManagerEITCmd = &cobra.Command{
	Use: "list",
	Short: "List K8s Cert Manager YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Long: "List K8s Cert Manager YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "K8s Cert Manager", util.K8sCertManagerCertificateType)
	},
}

func init() {
	listK8sCertManagerEITCmd.Flags().SortFlags = false
}
