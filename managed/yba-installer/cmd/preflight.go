package cmd

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
)

var (
	skippedPreflightChecks []string
	upgradePreflightChecks bool
	migratePreflightChecks bool
)

var preflightCmd = &cobra.Command{
	Use: "preflight",
	Short: "Run preflight checks to make sure that your system is ready to " +
		"install YugabyteDB Anywhere.",
	Long: `
        The preflight command goes through a series of Preflight checks and alerts you if these
				requirements are not met. Preflight checks are also run during install and upgrade.`,
	Args: cobra.MaximumNArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		// Print all known checks
		// TODO: make the user specify which type of preflight list to specify
		var checksToRun []preflight.Check
		if viper.GetBool("ybdb.install.enabled") {
			//TODO: Add preflight checks for YBDB.
			checksToRun = preflight.InstallChecks
		} else {
			checksToRun = preflight.InstallChecksWithPostgres
		}
		if len(args) == 1 && args[0] == "list" {
			fmt.Println("Known preflight install checks:")
			for _, check := range checksToRun {
				fmt.Println("  " + check.Name())
			}
		} else {
			if upgradePreflightChecks {
				checksToRun = preflight.UpgradeChecks
			} else if migratePreflightChecks {
				checksToRun = preflight.ReplicatedMigrateChecks
			}
			// TODO: We should allow the user to better specify which checks to run.
			// Will do this as we implement a set of upgrade preflight checks
			results := preflight.Run(checksToRun, skippedPreflightChecks...)
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
		[]string{}, "Preflight checks to skip by name")
	preflightCmd.Flags().BoolVar(&upgradePreflightChecks, "upgrade", false,
		"run preflight checks for upgrade")
	preflightCmd.Flags().BoolVar(&migratePreflightChecks, "migrate", false,
		"run preflight checks for replicted migration")

	rootCmd.AddCommand(preflightCmd)
}
