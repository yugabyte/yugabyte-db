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

const migrateStartCmd = "yba-ctl replicated-migrate start"

// Get the commands that will require yba-ctl.yml to be setup before getting run
// function because go doesn't support const <slice>
func cmdsRequireConfigInit() []string {
	return []string{
		"yba-ctl install",
		"yba-ctl upgrade",
		"yba-ctl preflight",
		migrateStartCmd,
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

	if cmdName == migrateStartCmd {
		currRoot := viper.GetString("installRoot")
		if currRoot == "/opt/yugabyte" {
			log.Debug("Install root is the default /opt/yugabyte on migration. Setting to /opt/yba")
			common.UpdateRootInstall("/opt/yba")
		} else {
			prompt := fmt.Sprintf("Do you want to continue the Replicated migration to the custom "+
				"install root %s", currRoot)
			if !common.UserConfirm(prompt, common.DefaultYes) {
				log.Info("Canceling replicated migration")
				os.Exit(1)
			}
		}
	}

	log.AddOutputFile(common.YbactlLogFile())
	log.Trace(fmt.Sprintf("yba-ctl started with cmd %s", cmdName))

	initServices(cmdName)
}

func writeDefaultConfig() {
	cfgFile, err := os.Create(common.InputFile())
	if err != nil {
		log.Fatal("could not create input file: " + err.Error())
	}
	defer cfgFile.Close()

	_, err = cfgFile.WriteString(config.ReferenceYbaCtlConfig)
	if err != nil {
		log.Fatal("could not create input file: " + err.Error())
	}
	err = os.Chmod(common.InputFile(), 0644)
	if err != nil {
		log.Warn("failed to update config file permissions: " + err.Error())
	}
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
		writeDefaultConfig()

		if !userChoice {
			log.Info(fmt.Sprintf(
				"Aborting current command. Please edit the config at %s and retry.", common.InputFile()))
			os.Exit(1)
		}
	}
}

func initServices(cmdName string) {
	// services is an ordered map so services that depend on others should go later in the chain.
	services = make(map[string]common.Component)
	installPostgres := viper.GetBool("postgres.install.enabled")
	installYbdb := viper.GetBool("ybdb.install.enabled")
	services[PostgresServiceName] = NewPostgres("10.23")
	// services[YbdbServiceName] = NewYbdb("2.17.2.0")
	services[PrometheusServiceName] = NewPrometheus("2.44.0")
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
