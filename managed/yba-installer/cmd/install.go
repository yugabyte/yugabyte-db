package cmd

import (
	"github.com/spf13/cobra"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/preflight"
)

var skippedPreflightChecks []string

var installCmd = &cobra.Command{
	Use:   "install",
	Short: "The install command is installs Yugabyte Anywhere onto your operating system.",
	Long: `
        The install command is the main workhorse command for YBA Installer that
        will install the version of Yugabyte Anywhere associated with your downloaded version
        of YBA Installer onto your host Operating System. Can also perform an install while skipping
        certain preflight checks if desired.
        `,
	Args: cobra.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		if force {
			common.DisableUserConfirm()
		}
		// Preflight checks
		results := preflight.Run(preflight.InstallChecksWithPostgres, skippedPreflightChecks...)
		// Only print results if we should fail.
		if preflight.ShouldFail(results) {
			preflight.PrintPreflightResults(results)
			log.Fatal("preflight failed")
		}

		// Run install
		common.Install(common.GetVersion())

		for _, name := range serviceOrder {
			services[name].Install()
		}

		for _, name := range serviceOrder {
			status := services[name].Status()
			if status.Status != common.StatusRunning {
				log.Fatal(status.Service + " is not running! Install failed")
			}
		}

		common.PostInstall()
		log.Info("Successfully installed Yugabyte Anywhere!")
	},
}

func init() {
	installCmd.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip")
	rootCmd.AddCommand(installCmd)
}
