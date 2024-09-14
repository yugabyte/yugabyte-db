/*
 * Copyright (c) YugaByte, Inc.
 */

package k8scertmanager

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// K8sCertManagerEITCmd represents the eit command
var K8sCertManagerEITCmd = &cobra.Command{
	Use:     "k8s-cert-manager",
	GroupID: "type",
	Short: "Manage a YugabyteDB Anywhere K8s Cert Manager encryption" +
		" in transit (EIT) certificate configuration",
	Long: "Manage a K8s Cert Manager encryption in transit (EIT)" +
		" certificate configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	K8sCertManagerEITCmd.Flags().SortFlags = false

	K8sCertManagerEITCmd.AddCommand(createK8sCertManagerEITCmd)
	K8sCertManagerEITCmd.AddCommand(listK8sCertManagerEITCmd)
	K8sCertManagerEITCmd.AddCommand(describeK8sCertManagerEITCmd)
	K8sCertManagerEITCmd.AddCommand(deleteK8sCertManagerEITCmd)

	K8sCertManagerEITCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}
