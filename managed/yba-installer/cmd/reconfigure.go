package cmd

import (
	"os"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/components/ybactl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/components/yugaware"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var reconfigureCmd = &cobra.Command{
	Use: "reconfigure",
	Short: "The reconfigure command is used to apply changes made to yba-ctl.yml to running " +
		"YugabyteDB Anywhere services.",
	Args: cobra.NoArgs,
	Long: `
    The reconfigure command is used to apply changes made to yba-ctl.yml to running 
	YugabyteDB Anywhere services. The process involves restarting all associated services.`,
	PreRun: func(cmd *cobra.Command, args []string) {
		if !skipVersionChecks {
			yugawareVersion, err := yugaware.InstalledVersionFromMetadata()
			if err != nil {
				log.Fatal("Cannot reconfigure: " + err.Error())
			}
			if yugawareVersion != ybactl.Version {
				log.Fatal("yba-ctl version does not match the installed YugabyteDB Anywhere version")
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		for _, name := range serviceOrder {
			log.Info("Stopping service " + name)
			services[name].Stop()
		}

		// Change into the dir we are in so that we can specify paths relative to ourselves
		// TODO(minor): probably not a good idea in the long run
		os.Chdir(common.GetBinaryDir())

		for _, name := range serviceOrder {
			log.Info("Regenerating config for service " + name)
			config.GenerateTemplate(services[name])
			log.Info("Starting service " + name)
			services[name].Start()
		}

		for _, name := range serviceOrder {
			status, err := services[name].Status()
			if err != nil {
				log.Fatal("Failed to get status: " + err.Error())
			}
			if !common.IsHappyStatus(status) {
				log.Fatal(status.Service + " is not running! Restart might have failed, please check " +
					common.YbactlLogFile())
			}
		}

	},
}

func init() {
	rootCmd.AddCommand(reconfigureCmd)
}
