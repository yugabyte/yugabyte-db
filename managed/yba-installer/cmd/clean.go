package cmd

import (
	"log"
	"path/filepath"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

func cleanCmd() *cobra.Command {
	var removeData bool
	clean := &cobra.Command{
		Use:   "clean [--all]",
		Short: "The clean command uninstalls your YugabyteDB Anywhere instance.",
		Long: `
    	The clean command performs a complete removal of your YugabyteDB Anywhere
    	Instance by stopping all services and (optionally) removing data directories.`,
		Aliases: []string{"uninstall"},
		Args:    cobra.MaximumNArgs(1),
		// Confirm with the user before deleting ALL data.
		PreRun: func(cmd *cobra.Command, args []string) {
			// Version check before clean
			if !common.RunFromInstalled() {
				path := filepath.Join(common.YbactlInstallDir(), "yba-ctl")
				log.Fatal("clean must be run from " + path +
					". It may be in the systems $PATH for easy of use.")
			}

			// Prompt for remmoving all data
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
	rootCmd.AddCommand(cleanCmd())
}
