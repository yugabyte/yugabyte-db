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
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
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

// List of services required for YBA installation.
var services map[string]common.Component
var serviceOrder []string

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
	if !(strings.HasPrefix(cmdName, "yba-ctl replicated-migrate") ||
		strings.HasPrefix(cmdName, "yba-ctl license")) {
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
	services = make(map[string]common.Component)
	installPostgres := viper.GetBool("postgres.install.enabled")
	installYbdb := viper.GetBool("ybdb.install.enabled")
	services[PostgresServiceName] = NewPostgres("14.11")
	// services[YbdbServiceName] = NewYbdb("2.17.2.0")
	services[PrometheusServiceName] = NewPrometheus("2.47.1")
	services[YbPlatformServiceName] = NewPlatform(ybactl.Version)
	// serviceOrder = make([]string, len(services))
	if installPostgres {
		serviceOrder = []string{PostgresServiceName, PrometheusServiceName, YbPlatformServiceName}
	} else if installYbdb {
		serviceOrder = []string{YbdbServiceName, PrometheusServiceName, YbPlatformServiceName}
	} else {
		serviceOrder = []string{PrometheusServiceName, YbPlatformServiceName}
	}
	// populate names of services for valid args
}

func handleRootCheck(cmdName string) {
	user, err := osuser.Current()
	if err != nil {
		log.Fatal("Could not determine current user: " + err.Error())
	}

	// First, validate that the user (root access) matches the config 'as_root'
	if user.Uid == "0" && !viper.GetBool("as_root") {
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
