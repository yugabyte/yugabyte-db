package cmd

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/preflight"
)

var upgradeCmd = &cobra.Command{
	Use:   "upgrade",
	Short: "The upgrade command is used to upgrade an existing Yugabyte Anywhere installation.",
	Long: `
   The execution of the upgrade command will upgrade an already installed version of Yugabyte
   Anywhere present on your operating system, to the upgrade version associated with your download
	 of YBA Installer. Please make sure that you have installed Yugabyte Anywhere using the install
	 command prior to executing the upgrade command.
   `,
	Args: cobra.NoArgs,
	// We will use prerun to do some basic setup for the upcoming upgrade.
	// At this point, its making sure Directory Manager is set to do an upgrade.
	PreRun: func(cmd *cobra.Command, args []string) {
		// TODO: IMO this is error prone - as in it can be easy to forget we need
		// to change the directory manager workflow. In the future, I think we should have
		// some sort of config that is given to all structs we create, and based on that be able to
		// chose the correct workflow.
		common.SetWorkflowUpgrade()
	},
	Run: func(cmd *cobra.Command, args []string) {
		errors := preflight.Run(preflight.UpgradeChecks)
		if len(errors) > 0 {
			preflight.PrintPreflightResults(errors)
			log.Fatal("all preflight checks must pass to upgrade")
		}

		/* This is the postgres major version upgrade workflow!
		// First, stop platform and prometheus. Postgres will need to be running
		// to take the backup for postgres upgrade.
		services[YbPlatformServiceName].Stop()
		services[PrometheusServiceName].Stop()

		common.Upgrade(common.GetVersion())

		for _, name := range serviceOrder {
			services[name].Upgrade()
		}

		for _, name := range serviceOrder {
			status := services[name].Status()
			if status.Status != common.StatusRunning {
				log.Fatal(status.Service + " is not running! upgrade failed")
			}
		}
		*/

		// Here is the postgres minor version/no upgrade workflow
		common.Upgrade(common.GetVersion())
		for _, name := range serviceOrder {
			services[name].Upgrade()
		}

		for _, name := range serviceOrder {
			services[name].Stop()
			services[name].Start()
		}

		for _, name := range serviceOrder {
			status := services[name].Status()
			if status.Status != common.StatusRunning {
				log.Fatal(status.Service + " is not running! upgrade failed")
			}
		}
		// Here ends the postgres minor version/no upgrade workflow

		common.CreateInstallMarker()
		common.SetActiveInstallSymlink()

	},
}

func init() {
	rootCmd.AddCommand(upgradeCmd)
}
