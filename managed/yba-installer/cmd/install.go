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

		// Install the license if it is provided.
		if licensePath != "" {
			InstallLicense()
		}

		// Preflight checks
		results := preflight.Run(preflight.InstallChecksWithPostgres, skippedPreflightChecks...)
		// Only print results if we should fail.
		if preflight.ShouldFail(results) {
			preflight.PrintPreflightResults(results)
			log.Fatal("Preflight checks failed. To skip (not recommended), " +
				"rerun the command with --skip_preflight <check name1>,<check name2>")
		}

		// Run install
		common.Install(common.GetVersion())

		for _, name := range serviceOrder {
			log.Info("About to install component " + name)
			services[name].Install()
			log.Info("Completed installing component " + name)
		}

		var statuses []common.Status
		for _, service := range services {
			status := service.Status()
			statuses = append(statuses, status)
			if !common.IsHappyStatus(status) {
				log.Fatal(status.Service + " is not running! Install might have failed, please check " + common.YbaCtlLogFile)
			}
		}

		common.PostInstall()
		common.PrintStatus(statuses...)
		log.Info("Successfully installed Yugabyte Anywhere!")
	},
}

func init() {
	installCmd.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip")
	installCmd.Flags().StringVarP(&licensePath, "license-path", "l", "", "path to license file")

	// Install must be run from directory of yba version
	if !common.RunFromInstalled() {
		rootCmd.AddCommand(installCmd)
	}
}
