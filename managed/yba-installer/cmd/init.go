/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// Get the commands that will require yba-ctl.yml to be setup before getting run
// function because go doesn't support const <slice>
func cmdsRequireConfigInit() []string {
	return []string{
		"yba-ctl install",
		"yba-ctl license",
		"yba-ctl license update",
		"yba-ctl upgrade",
	}
}

// List of services required for YBA installation.
var services map[string]common.Component
var serviceOrder []string

func initAfterFlagsParsed(cmdName string) {
	if force {
		common.DisableUserConfirm()
	}

	// init logging and set log level for stdout
	log.Init(logLevel)

	// verify that we have a conf file
	if common.Contains(cmdsRequireConfigInit(), cmdName) {
		ensureInstallerConfFile()
	}

	common.InitViper()

	log.AddOutputFile(common.YbaCtlLogFile)
	log.Trace(fmt.Sprintf("yba-ctl started with cmd %s", cmdName))

	initServices()
}

func ensureInstallerConfFile() {
	_, err := os.Stat(common.InputFile)
	if err != nil && !os.IsNotExist(err) {
		log.Fatal("Unexpected error reading file " + common.InputFile)
	}

	if os.IsNotExist(err) {
		userChoice := common.UserConfirm(
			fmt.Sprintf(
				("Using default settings in config file %s."+
					"Note that some settings cannot be changed later. \n\n"+
					"Proceed with default config? "),
				common.InputFile),
			common.DefaultNo)

		// Copy over reference yaml before checking the user choice.
		common.MkdirAllOrFail(filepath.Dir(common.InputFile), 0755)
		common.CopyFile(common.GetReferenceYaml(), common.InputFile)
		os.Chmod(common.InputFile, 0600)

		if !userChoice {
			log.Info(fmt.Sprintf(
				"Aborting current command. Please edit the config at %s and retry.", common.InputFile))
			os.Exit(1)
		}

	}
}

func initServices() {
	// services is an ordered map so services that depend on others should go later in the chain.
	services = make(map[string]common.Component)
	installPostgres := viper.GetBool("postgres.install.enabled")
	services[PostgresServiceName] = NewPostgres("10.23")
	services[PrometheusServiceName] = NewPrometheus("2.41.0")
	services[YbPlatformServiceName] = NewPlatform(common.GetVersion())
	// serviceOrder = make([]string, len(services))
	if installPostgres {
		serviceOrder = []string{PostgresServiceName, PrometheusServiceName, YbPlatformServiceName}
	} else {
		serviceOrder = []string{PrometheusServiceName, YbPlatformServiceName}
	}
	// populate names of services for valid args
}
