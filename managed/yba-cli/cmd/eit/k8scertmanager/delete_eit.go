/*
 * Copyright (c) YugaByte, Inc.
 */

package k8scertmanager

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteK8sCertManagerEITCmd represents the eit command
var deleteK8sCertManagerEITCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere K8s Cert Manager encryption in transit configuration",
	Long:  "Delete a K8s Cert Manager encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITUtil(cmd, "K8s Cert Manager", util.K8sCertManagerCertificateType)
	},
}

func init() {
	deleteK8sCertManagerEITCmd.Flags().SortFlags = false
	deleteK8sCertManagerEITCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
