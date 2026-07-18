/*
 * Copyright (c) YugabyteDB, Inc.
 */

package k8scertmanager

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listK8sCertManagerEITCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short: "List K8s Cert Manager YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Long: "List K8s Cert Manager YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Example: `yba eit k8s-cert-manager list`,
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.ListEITUtil(cmd, "K8s Cert Manager", util.K8sCertManagerCertificateType)
	},
}

func init() {
	listK8sCertManagerEITCmd.Flags().SortFlags = false
}
