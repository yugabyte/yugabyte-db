/*
 * Copyright (c) YugabyteDB, Inc.
 */

package template

import (
	"github.com/spf13/cobra"
)

// TemplateAlertCmd set of commands are used to perform operations on templates
// in YugabyteDB Anywhere
var TemplateAlertCmd = &cobra.Command{
	Use:   "template",
	Short: "Manage YugabyteDB Anywhere alert templates",
	Long:  "Manage YugabyteDB Anywhere alert templates",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	TemplateAlertCmd.PersistentFlags().SortFlags = false
	TemplateAlertCmd.Flags().SortFlags = false

	TemplateAlertCmd.AddCommand(listTemplateAlertCmd)
	//  TemplateAlertCmd.AddCommand(describeTemplateAlertCmd)
	//  TemplateAlertCmd.AddCommand(deleteTemplateAlertCmd)
	//  TemplateAlertCmd.AddCommand(createTemplateAlertCmd)
	//  TemplateAlertCmd.AddCommand(updateTemplateAlertCmd)
	//  TemplateAlertCmd.AddCommand(testAlertTemplateAlertCmd)

}
