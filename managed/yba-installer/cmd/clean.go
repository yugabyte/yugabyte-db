package cmd

import (
	"log"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
)

func cleanCmd() *cobra.Command {
	var removeData bool
	clean := &cobra.Command{
		Use:   "clean [--all]",
		Short: "The clean command uninstalls your YugabyteDB Anywhere instance.",
		Long: `
    	The clean command performs a complete removal of your YugabyteDB Anywhere
    	Instance by stopping all services and (optionally) removing data directories.`,
		Args: cobra.MaximumNArgs(1),
		// Confirm with the user before deleting ALL data.
		PreRun: func(cmd *cobra.Command, args []string) {
			if removeData {
				prompt := "--all was specified. This will delete all data with no way to recover. Continue?"
				if !common.UserConfirm(prompt, common.DefaultNo) {
					log.Fatal("Stopping clean")
				}
			}
		},
		Run: func(cmd *cobra.Command, args []string) {

			// TODO: Only clean up per service.
			// Clean up services in reverse order.
			serviceNames := []string{}
			for i := len(serviceOrder) - 1; i >= 0; i-- {
				services[serviceOrder[i]].Uninstall(removeData)
				serviceNames = append(serviceNames, services[serviceOrder[i]].Name())
			}

			common.Uninstall(serviceNames, removeData)
		},
	}
	clean.Flags().BoolVar(&removeData, "all", false, "also clean out data (default: false)")
	return clean
}

func init() {
	// Clean must be run from installed yba-ctl
	if common.RunFromInstalled() {
		rootCmd.AddCommand(cleanCmd())
	}
}
