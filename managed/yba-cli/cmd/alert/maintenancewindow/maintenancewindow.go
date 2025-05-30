/*
 * Copyright (c) YugaByte, Inc.
 */

package maintenancewindow

import "github.com/spf13/cobra"

// MaintenanceWindowCmd set of commands are used to perform operations on policies
// in YugabyteDB Anywhere
var MaintenanceWindowCmd = &cobra.Command{
	Use:     "maintenance-window",
	Aliases: []string{"maintenancewindow"},
	Short:   "Manage YugabyteDB Anywhere maintenance window",
	Long:    "Manage YugabyteDB Anywhere maintenance window",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	MaintenanceWindowCmd.PersistentFlags().SortFlags = false
	MaintenanceWindowCmd.Flags().SortFlags = false

	MaintenanceWindowCmd.AddCommand(listMaintenanceWindowCmd)
	MaintenanceWindowCmd.AddCommand(describeMaintenanceWindowCmd)
	MaintenanceWindowCmd.AddCommand(deleteMaintenanceWindowCmd)
	MaintenanceWindowCmd.AddCommand(createMaintenanceWindowCmd)
	MaintenanceWindowCmd.AddCommand(updateMaintenanceWindowCmd)

}
