/*
 * Copyright (c) YugabyteDB, Inc.
 */

package customer

import (
	"github.com/spf13/cobra"
)

// CustomerCmd set of commands are used to perform operations on customers
// in YugabyteDB Anywhere
var CustomerCmd = &cobra.Command{
	Use:   "customer",
	Short: "Manage YugabyteDB Anywhere customers",
	Long:  "Manage YugabyteDB Anywhere customers",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	CustomerCmd.PersistentFlags().SortFlags = false
	CustomerCmd.Flags().SortFlags = false

	CustomerCmd.AddCommand(listCustomerCmd)
	CustomerCmd.AddCommand(describeCustomerCmd)

}
