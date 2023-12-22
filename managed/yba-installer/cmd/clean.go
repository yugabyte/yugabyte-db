package cmd

import (
	// "path/filepath"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
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

			// Prompt for remmoving all data
			if removeData {
				prompt := "--all was specified. This will delete all data with no way to recover. Continue?"
				if !common.UserConfirm(prompt, common.DefaultNo) {
					log.Fatal("Stopping clean")
				}
			}
		},
		Run: func(cmd *cobra.Command, args []string) {
			state, err := ybactlstate.Initialize()
			if err != nil {
				log.Warn("failed to load internal state, continue with uninstall")
				state = ybactlstate.New()
			}
			if err := state.TransitionStatus(ybactlstate.CleaningStatus); err != nil {
				log.Fatal("Could not start clean: " + err.Error())
			}

			// TODO: Only clean up per service.
			// Clean up services in reverse order.
			serviceNames := []string{}
			for i := len(serviceOrder) - 1; i >= 0; i-- {
				services[serviceOrder[i]].Uninstall(removeData)
				serviceNames = append(serviceNames, services[serviceOrder[i]].Name())
			}

			common.Uninstall(serviceNames, removeData)
			// If this is a soft clean, update internal state. Otherwise skip, as there is nothing left to
			// update.
			if !removeData {
				if err := state.TransitionStatus(ybactlstate.SoftCleanStatus); err != nil {
					log.Warn("failed to update internal state - continue with uninstall")
				}
			}
		},
	}
	clean.Flags().BoolVar(&removeData, "all", false, "also clean out data (default: false)")
	return clean
}

func init() {
	rootCmd.AddCommand(cleanCmd())
}
