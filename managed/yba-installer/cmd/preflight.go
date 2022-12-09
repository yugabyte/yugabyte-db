package cmd

import (
	"fmt"

	"github.com/spf13/cobra"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/preflight"
)

var preflightCmd = &cobra.Command{
	Use: "preflight",
	Short: "The preflight command checks makes sure that your system is ready to " +
		"install Yugabyte Anywhere.",
	Long: `
        The preflight command goes through a series of Preflight checks that each have a
        critcal and warning level, and alerts you if these requirements are not met on your
        Operating System.`,
	Args: cobra.MaximumNArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		// Print all known checks
		// TODO: make the user specify which type of preflight list to specify
		if len(args) == 1 && args[0] == "list" {
			fmt.Println("Known preflight install checks:")
			for _, check := range preflight.InstallChecksWithPostgres {
				fmt.Println("  " + check.Name())
			}
		} else {
			// TODO: We should allow the user to better specify which checks to run.
			// Will do this as we implement a set of upgrade preflight checks
			results := preflight.Run(preflight.InstallChecksWithPostgres, skippedPreflightChecks...)
			preflight.PrintPreflightResults(results)
			if preflight.ShouldFail(results) {
				log.Fatal("preflight failed")
			}
		}
	},
}

func init() {
	// skippedPreflightChecks is defined in install, but is useful here too
	preflightCmd.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip")

	rootCmd.AddCommand(preflightCmd)
}
