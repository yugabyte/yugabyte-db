/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"os"
	osuser "os/user"
	"strings"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight/checks"
)

var (
	PostgresVersion    string = ""
	PrometheusVersion  string = ""
	PerfAdvisorVersion string = ""
)

// Get the commands that will require yba-ctl.yml to be setup before getting run
// function because go doesn't support const <slice>
func cmdsRequireConfigInit() []string {
	return []string{
		"yba-ctl install",
		"yba-ctl upgrade",
		"yba-ctl preflight",
	}
}

var ybaCtl *ybactl.YbaCtlComponent

func initAfterFlagsParsed(cmdName string) {
	// init logging and set log level for stdout
	log.Init(logLevel)

	if force {
		common.DisableUserConfirm()
	}
	// verify that we have a conf file, or create it if necessary
	if common.Contains(cmdsRequireConfigInit(), cmdName) {
		ensureInstallerConfFile()
	}

	// Load the config after confirming config file
	common.InitViper()

	// Replicated migration handles config and root checks on its own.
	// License does not need any config, so skip for now
	// Generate config will create the config, and set as_root as needed.
	if !(strings.HasPrefix(cmdName, "yba-ctl replicated-migrate") ||
		strings.HasPrefix(cmdName, "yba-ctl license") ||
		strings.HasPrefix(cmdName, "yba-ctl generate-config")) {
		handleRootCheck(cmdName)
	}

	// Finally, do basic yba-ctl setup
	ybaCtl = ybactl.New()
	ybaCtl.Setup()
	initServices()

	// Add logging now that yba-ctl directory is created
	log.AddOutputFile(common.YbactlLogFile())

	log.Trace(fmt.Sprintf("yba-ctl started with cmd %s", cmdName))
}

func ensureInstallerConfFile() {
	_, err := os.Stat(common.InputFile())
	if err != nil && !os.IsNotExist(err) {
		log.Fatal("Unexpected error reading file " + common.InputFile())
	}

	if os.IsNotExist(err) {
		userChoice := common.UserConfirm(
			fmt.Sprintf(
				("No config file found at '%s', creating it with default values now.\n"+
					"Do you want to proceed with the default config?"),
				common.InputFile()),
			common.DefaultNo)

		// Copy over reference yaml before checking the user choice.
		config.WriteDefaultConfig()

		if !userChoice {
			log.Info(fmt.Sprintf(
				"Aborting current command. Please edit the config at %s and retry.", common.InputFile()))
			os.Exit(1)
		}
	}
}

func initServices() {
	// services is an ordered map so services that depend on others should go later in the chain.
	serviceManager.RegisterService(NewPostgres(PostgresVersion))
	serviceManager.RegisterService(NewPrometheus(PrometheusVersion))
	serviceManager.RegisterService(NewPlatform(ybactl.Version))
	serviceManager.RegisterService(NewPerfAdvisor(PerfAdvisorVersion))
	serviceManager.RegisterService(NewLogRotate())
	var services []components.Service
	for s := range serviceManager.Services() {
		services = append(services, s)
	}
	checks.SetServicesRunningCheck(services)
}

func handleRootCheck(cmdName string) {
	user, err := osuser.Current()
	if err != nil {
		log.Fatal("Could not determine current user: " + err.Error())
	}

	if !viper.IsSet("as_root") {
		// Upgrading from before "as_root" exists. /opt/yba-ctl is source of truth, not viper
		_, err := os.Stat(common.YbactlRootInstallDir)
		if user.Uid == "0" && err != nil {
			log.Fatal("no root install found at /opt/yba-ctl, cannot upgrade with root")
		} else if user.Uid != "0" && err == nil {
			log.Fatal("Detected root install at /opt/yba-ctl, cannot upgrade as non-root")
		}
		log.Debug(fmt.Sprintf("legacy root check passed for %s", cmdName))

		// Also handle the case where a config file is provided but did not include as_root
		if err := common.SetYamlValue(common.InputFile(), "as_root", user.Uid == "0"); err != nil {
			log.Warn("Failed to set as_root in config file, please set it manually")
			log.Fatal("Failed to set as_root in config file: " + err.Error())
		}
		return
	} else if user.Uid == "0" && !viper.GetBool("as_root") {
		log.Fatal("running as root user with 'as_root' set to false is not supported")
	} else if user.Uid != "0" &&
		(viper.GetBool("as_root") || common.Exists(common.YbactlRootInstallDir)) {
		// Allow running certain commands as non-root in root install mode
		switch cmdName {
		case "yba-ctl version", "yba-ctl status", "yba-ctl createBackup":
			break
		default:
			log.Fatal("running as non-root user with 'as_root' set to true is not supported")
		}
	}
}

func init() {
	if PostgresVersion == "" {
		panic("PostgresVersion not set during build")
	}
	if PrometheusVersion == "" {
		panic("PrometheusVersion not set during build")
	}
}
