/*
 * Copyright (c) YugaByte, Inc.
 */

package k8scertmanager

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeK8sCertManagerEITCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short: "Describe a K8s Cert Manager YugabyteDB Anywhere" +
		" Encryption In Transit (EIT) configuration",
	Long: "Describe a K8s Cert Manager YugabyteDB Anywhere" +
		" Encryption In Transit (EIT) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DescribeEITUtil(cmd, "K8s Cert Manager", util.K8sCertManagerCertificateType)

	},
}

func init() {
	describeK8sCertManagerEITCmd.Flags().SortFlags = false
}
