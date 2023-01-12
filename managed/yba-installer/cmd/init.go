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
	ensureInstallerConfFile()

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
				("No config file found at %[2]s.\n\n"+
					"Proceed with default settings from reference conf file %[1]s?\n\n"+
					"Note that some settings cannot be modified post-installation.\n"+
					"To customize the default settings, \n"+
					"1. please copy the reference conf file at %[1]s to %[2]s,\n"+
					"2. change settings in %[2]s \n"+
					"and, 3. re-run the install command. \n"),
				common.GetReferenceYaml(), common.InputFile),
			common.DefaultNo)
		if !userChoice {
			log.Info("Aborting current command")
			os.Exit(0)
		}

		// Copy over reference yaml
		common.MkdirAllOrFail(filepath.Dir(common.InputFile), 0755)
		common.CopyFile(common.GetReferenceYaml(), common.InputFile)
		os.Chmod(common.InputFile, 0600)

	}
}

func initServices() {
	// services is an ordered map so services that depend on others should go later in the chain.
	services = make(map[string]common.Component)
	installPostgres := viper.GetBool("postgres.install.enabled")
	services[PostgresServiceName] = NewPostgres("10.23")
	services[PrometheusServiceName] = NewPrometheus("2.39.0")
	services[YbPlatformServiceName] = NewPlatform(common.GetVersion())
	// serviceOrder = make([]string, len(services))
	if installPostgres {
		serviceOrder = []string{PostgresServiceName, PrometheusServiceName, YbPlatformServiceName}
	} else {
		serviceOrder = []string{PrometheusServiceName, YbPlatformServiceName}
	}
	// populate names of services for valid args
}
