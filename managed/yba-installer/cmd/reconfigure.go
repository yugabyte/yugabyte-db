package cmd

import (
	"os"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/yugaware"
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
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("unable to load yba installer state: " + err.Error())
		}

		if err := state.ValidateReconfig(); err != nil {
			log.Fatal("invalid reconfigure: " + err.Error())
		}

		isSelfSigned := state.Config.SelfSignedCert ||
			(viper.GetString("server_cert_path") == "" && viper.GetString("server_key_path") == "")
		if state.Config.Hostname != viper.GetString("host") && isSelfSigned {
			log.Info("Detected hostname change for self signed certs, regenerating the certs")
			serverCertPath, serverKeyPath := common.RegenerateSelfSignedCerts()
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
