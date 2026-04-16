/*
 * Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/configuration/template"
)

// ConfigurationAlertCmd set of commands are used to perform operations on policies
// in YugabyteDB Anywhere
var ConfigurationAlertCmd = &cobra.Command{
	Use:     "policy",
	Aliases: []string{"configuration"},
	Short:   "Manage YugabyteDB Anywhere alert policies",
	Long:    "Manage YugabyteDB Anywhere alert policies",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ConfigurationAlertCmd.PersistentFlags().SortFlags = false
	ConfigurationAlertCmd.Flags().SortFlags = false

	ConfigurationAlertCmd.AddCommand(listConfigurationAlertCmd)
	ConfigurationAlertCmd.AddCommand(describeConfigurationAlertCmd)
	ConfigurationAlertCmd.AddCommand(deleteConfigurationAlertCmd)
	ConfigurationAlertCmd.AddCommand(createConfigurationAlertCmd)
	ConfigurationAlertCmd.AddCommand(updateConfigurationAlertCmd)
	ConfigurationAlertCmd.AddCommand(testAlertConfigurationAlertCmd)
	ConfigurationAlertCmd.AddCommand(template.TemplateAlertCmd)

}
