/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"io/fs"
	"os"

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
	if force {
		common.DisableUserConfirm()
	}

	handleRootCheck(cmdName)

	// init logging and set log level for stdout
	log.Init(logLevel)

	ybaCtl = ybactl.New()
	ybaCtl.Setup()

	// verify that we have a conf file
	if common.Contains(cmdsRequireConfigInit(), cmdName) {
		ensureInstallerConfFile()
	}

	common.InitViper()

	log.AddOutputFile(common.YbactlLogFile())
	log.Trace(fmt.Sprintf("yba-ctl started with cmd %s", cmdName))

	initServices()
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
	services[PostgresServiceName] = NewPostgres("14.9")
	// services[YbdbServiceName] = NewYbdb("2.17.2.0")
	services[PrometheusServiceName] = NewPrometheus("2.46.0")
	services[YbPlatformServiceName] = NewPlatform(common.GetVersion())
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
	switch cmdName {
	// Install should typically be done as root, and will confirm if the user does not.
	case "yba-ctl install":
		if !common.HasSudoAccess() {
			if !common.UserConfirm("Installing without root access is not recommend and should not be "+
				"done for production use. Do you want to continue install as non-root?", common.DefaultNo) {
				fmt.Println("Please run install as root")
				os.Exit(1)
			}
		}
	default:
		if _, err := os.Stat(common.YbactlRootInstallDir); !errors.Is(err, fs.ErrNotExist) {
			// If /opt/yba-ctl/yba-ctl.yml exists, it was put there be root?
			if !common.HasSudoAccess() {
				fmt.Println("Please run yba-ctl as root")
				os.Exit(1)
			}
		}
	}
}
