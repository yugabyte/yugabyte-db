package cmd

import (
	"os"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var migrateDbCmd = &cobra.Command{
	Use: "migrate_db",
	Short: `The migrate_db command is an experimental feature to migrate
		data between postgres and ybdb. `,
	Args: cobra.NoArgs,
	Long: `
    The migrate_db command is an experimental command to migrate data
	between postgres and ybdb. It reads the db config changes made to
	yba-ctl.yml and and determines whether to migrate from YBDB to Postgres,
	vice versa, or take no action. The process restarts the yb-platform service.`,
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("unable to load yba installer state: " + err.Error())
		}

		if err := state.ValidateReconfig(); err != nil {
			log.Fatal("invalid config: " + err.Error())
		}

		dbMigrateFlow := state.GetDbUpgradeWorkFlow()

		if dbMigrateFlow == ybactlstate.PgToPg || dbMigrateFlow == ybactlstate.YbdbToYbdb {
			log.Info("Found no change in db config.")
			return
		}

		// Remove DB service from service Order
		serviceOrder = serviceOrder[1:]

		for _, name := range serviceOrder {
			log.Info("Stopping service " + name)
			services[name].Stop()
		}

		os.Chdir(common.GetBinaryDir())

		var newDbServiceName string
		if dbMigrateFlow == ybactlstate.PgToYbdb {
			migratePgToYbdbOrFatal()
			state.Postgres.IsEnabled = false
			state.Ybdb.IsEnabled = true
			newDbServiceName = YbdbServiceName
		} else {
			migrateYbdbToPgOrFatal()
			state.Postgres.IsEnabled = true
			state.Ybdb.IsEnabled = false
			newDbServiceName = PostgresServiceName
		}

		for _, name := range serviceOrder {
			log.Info("Regenerating config for service " + name)
			config.GenerateTemplate(services[name])
			log.Info("Starting service " + name)
			services[name].Start()
		}

		serviceOrder = append([]string{newDbServiceName}, serviceOrder...)

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

		//Update state
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("failed to write state: " + err.Error())
		}
	},
}

func init() {
	//Hide this command behind YBA_MODE=dev env flag.
	if os.Getenv("YBA_MODE") == "dev" {
		rootCmd.AddCommand(migrateDbCmd)
	}
}
