package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
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
		if !common.RunFromInstalled() {
			path := filepath.Join(common.YbactlInstallDir(), "yba-ctl")
			log.Fatal("reconfigure must be run from " + path +
				". It may be in the systems $PATH for easy of use.")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("unable to load yba installer state: " + err.Error())
		}

		if err := state.ValidateReconfig(); err != nil {
			log.Fatal("invalid reconfigure: " + err.Error())
		}

		// Regenerate self signed certs if hostname has changed or if certs are missing from the config.
		var serverCertPath, serverKeyPath string = "", ""
		if viper.GetString("server_cert_path") == "" || viper.GetString("server_key_path") == "" {
			log.Info("Generating new self-signed server certificates")
			serverCertPath, serverKeyPath = common.GenerateSelfSignedCerts()
		} else if state.Config.Hostname != viper.GetString("host") && state.Config.SelfSignedCert {
			log.Info("Regenerating self signed certs for hostname change")
			serverCertPath, serverKeyPath = common.RegenerateSelfSignedCerts()
		}
		if serverCertPath != "" || serverKeyPath != "" {
			log.Debug("Populating new self signed certs in yba-ctl.yml: " +
				serverCertPath + ", " + serverKeyPath)
			common.SetYamlValue(common.InputFile(), "server_cert_path", serverCertPath)
			common.SetYamlValue(common.InputFile(), "server_key_path", serverKeyPath)
			common.InitViper()
			state.Config.Hostname = viper.GetString("host")
			state.Config.SelfSignedCert = true // Ensure we track self signed certs after reconfig
		}

		if err := createPemFormatKeyAndCert(); err != nil {
			log.Fatal("failed to create server.pem: " + err.Error())
		}

		for _, name := range serviceOrder {
			log.Info("Stopping service " + name)
			services[name].Stop()
		}

		// Change into the dir we are in so that we can specify paths relative to ourselves
		// TODO(minor): probably not a good idea in the long run
		os.Chdir(common.GetBinaryDir())

		// Set any necessary config values due to changes
		if err := common.FixConfigValues(); err != nil {
			log.Fatal(fmt.Sprintf("Error changing default config values: %s", err.Error()))
		}

		for _, name := range serviceOrder {
			log.Info("Regenerating config for service " + name)
			config.GenerateTemplate(services[name])
			if name == PrometheusServiceName {
				// Fix up basic auth
				prom := services[name].(Prometheus)
				if err := prom.FixBasicAuth(); err != nil {
					log.Fatal("failed to edit basic auth: " + err.Error())
				}
			}
			if name == PostgresServiceName {
				// Make sure postgres is configured correctly
				pg := services[name].(Postgres)
				pg.modifyPostgresConf()
			}
			// Set permissions to be safe
			if err := common.SetAllPermissions(); err != nil {
				log.Fatal("error updating permissions for data and software directories: " + err.Error())
			}
			log.Info("Starting service " + name)
			services[name].Start()
		}

		if err := common.WaitForYBAReady(ybaCtl.Version()); err != nil {
			log.Fatal(err.Error())
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

		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("failed to write state: " + err.Error())
		}
	},
}

var configGenCmd = &cobra.Command{
	Use:     "generate-config",
	Short:   "Create the default config file.",
	Aliases: []string{"gen-config", "create-config"},
	Run: func(cmd *cobra.Command, args []string) {
		config.WriteDefaultConfig()
	},
}

func init() {
	rootCmd.AddCommand(reconfigureCmd, configGenCmd)
}
