package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"slices"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var reconfigureCmd = &cobra.Command{
	Use: "reconfigure [serviceName]",
	Short: "The reconfigure command is used to apply changes made to yba-ctl.yml to running " +
		"YugabyteDB Anywhere services.",
	Args: cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	// postgres is excluded: yb-platform and yb-perf-advisor depend on it.
	ValidArgs: []string{YbPlatformServiceName, PrometheusServiceName, PerfAdvisorServiceName,
		ByocApiProxyServiceName},
	Long: `
    The reconfigure command applies changes made to yba-ctl.yml to the running YugabyteDB
    Anywhere services. Without arguments it restarts every service. With a service name it
    restarts only that service and leaves the others running. Changes to the server certificate
    settings need a full reconfigure.`,
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

		// Validate the edited config before anything is applied to the running install.
		results := preflight.Run(preflight.ReconfigureChecks, skippedPreflightChecks...)
		if preflight.ShouldFail(results) {
			preflight.PrintPreflightResults(results)
			log.Fatal("Preflight checks failed. To skip (not recommended), " +
				"rerun the command with --skip_preflight <check name1>,<check name2>")
		}

		var services []components.Service
		if len(args) == 1 {
			services = prepareServiceReconfigure(args[0], state)
		} else {
			services = prepareFullReconfigure(state)
		}
		// An optional service that was just disabled is uninstalled and leaves nothing to restart.
		if len(services) > 0 {
			reconfigureServices(services)
			getAndPrintStatus(state, services)
		}

		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("failed to write state: " + err.Error())
		}
	},
}

// prepareFullReconfigure installs or uninstalls the optional services whose enabled flag changed,
// applies server certificate changes, and returns every service in the install for restart.
func prepareFullReconfigure(state *ybactlstate.State) []components.Service {
	for _, opt := range optionalServices {
		handleEnabledFlagChange(opt.name, opt.installed(state), opt.enabled())
		opt.setInstalled(state, opt.enabled())
	}
	if err := handleCertReconfig(state); err != nil {
		log.Fatal("failed to handle cert reconfig: " + err.Error())
	}
	return slices.Collect(serviceManager.Services())
}

// prepareServiceReconfigure is prepareFullReconfigure for a single service. It refuses server
// certificate changes: yb-platform, prometheus, yb-perf-advisor and node-exporter all serve the
// certificate, so a change has to reach every one of them.
func prepareServiceReconfigure(name string, state *ybactlstate.State) []components.Service {
	if certConfigChanged(state) {
		log.Fatal("server certificate settings in " + common.InputFile() + " changed; run " +
			"'yba-ctl reconfigure' without a service name to apply them to every service")
	}
	if opt := optionalServiceByName(name); opt != nil {
		wasInstalled, enabled := opt.installed(state), opt.enabled()
		handleEnabledFlagChange(name, wasInstalled, enabled)
		opt.setInstalled(state, enabled)
		if wasInstalled && !enabled {
			return nil
		}
	}
	if !serviceManager.Enabled(name) {
		log.Fatal(name + " is not enabled in " + common.InputFile())
	}
	return []components.Service{serviceManager.ServiceByName(name)}
}

// reconfigureServices stops the given services, regenerates their configuration from yba-ctl.yml
// and starts them again. Every service is stopped before any is started.
func reconfigureServices(services []components.Service) {
	// Always regenerate the server.pem file
	if err := createPemFormatKeyAndCert(); err != nil {
		log.Fatal("failed to create server.pem: " + err.Error())
	}

	for _, service := range services {
		log.Info("Stopping service " + service.Name())
		if err := service.Stop(); err != nil {
			log.Fatal("Failed to stop service " + service.Name() + ": " + err.Error())
		}
	}

	// Change into the dir we are in so that we can specify paths relative to ourselves
	// TODO(minor): probably not a good idea in the long run
	os.Chdir(common.GetBinaryDir())

	for _, service := range services {
		if err := service.Reconfigure(); err != nil {
			log.Fatal("Failed to reconfigure service " + service.Name() + ": " + err.Error())
		}
		// Set permissions to be safe
		if err := common.SetAllPermissions(); err != nil {
			log.Fatal("error updating permissions for data and software directories: " + err.Error())
		}
		log.Info("Starting service " + service.Name())
		if err := service.Start(); err != nil {
			log.Fatal("Failed to start service " + service.Name() + ": " + err.Error())
		}
	}

	for _, service := range services {
		if service.Name() == YbPlatformServiceName {
			if err := common.WaitForYBAReady(ybaCtl.Version()); err != nil {
				log.Fatal(err.Error())
			}
		}
	}
}

func handleEnabledFlagChange(serviceName string, wasEnabled, nowEnabled bool) {
	if wasEnabled == nowEnabled {
		return
	}
	service := serviceManager.ServiceByName(serviceName)
	if service == nil {
		log.Warn(serviceName + " service not found in service manager")
		return
	}
	if nowEnabled {
		log.Info(serviceName + " enabled changed from false to true. Installing " + serviceName + ".")
		if err := service.Install(); err != nil {
			log.Fatal("Failed to install " + serviceName + ": " + err.Error())
		}
		if err := service.Initialize(); err != nil {
			log.Fatal("Failed to initialize " + serviceName + ": " + err.Error())
		}
	} else {
		log.Info(serviceName + " enabled changed from true to false. Uninstalling " + serviceName + ".")
		if err := service.Uninstall(false); err != nil {
			log.Fatal("Failed to uninstall " + serviceName + ": " + err.Error())
		}
	}
}

func handleCertReconfig(state *ybactlstate.State) error {
	hasStateChange := false
	if isMoveToSelfSignedCert(state) {
		log.Info("Moving to self signed certs, generating new self-signed server certificates")
		hasStateChange = true
		state.Config.SelfSignedCert = true
		if err := common.GenerateSelfSignedCerts(); err != nil {
			return fmt.Errorf("failed to generate self signed certs during reconfigure: %w", err)
		}
	}
	if isMoveToCustomCert(state) {
		log.Info("Moving to custom certs")
		hasStateChange = true
		state.Config.SelfSignedCert = false
	}
	if isSelfSignedHostnameChange(state) {
		log.Info("Hostname has changed, regenerating self signed certs")
		hasStateChange = true
		state.Config.Hostname = viper.GetString("host")
		if err := common.RegenerateSelfSignedCerts(); err != nil {
			return fmt.Errorf("failed to regenerate self signed certs during reconfigure: %w", err)
		}
	}

	// Update the state file if any changes were made
	if hasStateChange {
		if err := ybactlstate.StoreState(state); err != nil {
			return fmt.Errorf("failed to write state: %w", err)
		}
	}
	return nil
}

// certConfigChanged reports whether yba-ctl.yml moved between self-signed and custom server
// certificates, or changed the hostname a self-signed certificate is issued for.
func certConfigChanged(state *ybactlstate.State) bool {
	return isMoveToSelfSignedCert(state) || isMoveToCustomCert(state) ||
		isSelfSignedHostnameChange(state)
}

func isMoveToSelfSignedCert(state *ybactlstate.State) bool {
	// Check if the server_cert_path and server_key_path are empty and state shows self signed is true
	return len(viper.GetString("server_cert_path")) == 0 &&
		len(viper.GetString("server_key_path")) == 0 &&
		!state.Config.SelfSignedCert
}

func isSelfSignedHostnameChange(state *ybactlstate.State) bool {
	// Check if the hostname has changed and state shows self signed is true
	return state.Config.Hostname != viper.GetString("host") && state.Config.SelfSignedCert
}

func isMoveToCustomCert(state *ybactlstate.State) bool {
	// Check if the server_cert_path and server_key_path are not empty and state shows self signed is false
	return len(viper.GetString("server_cert_path")) != 0 &&
		len(viper.GetString("server_key_path")) != 0 &&
		state.Config.SelfSignedCert
}

var configGenCmd = &cobra.Command{
	Use:     "generate-config",
	Short:   "Create the default config file. (alias: gen-config, create-config)",
	Aliases: []string{"gen-config", "create-config"},
	Run: func(cmd *cobra.Command, args []string) {
		if _, err := os.Stat(common.InputFile()); err == nil {
			prompt := fmt.Sprintf("Config file '%s' already exists, do you want to overwrite with a "+
				"default config?", common.InputFile())
			if !common.UserConfirm(prompt, common.DefaultNo) {
				log.Info("skipping generate-config")
				return
			}
		}
		config.WriteDefaultConfig()
	},
}

func init() {
	rootCmd.AddCommand(reconfigureCmd, configGenCmd)
	reconfigureCmd.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip by name")
}
