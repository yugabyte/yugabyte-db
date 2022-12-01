/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"fmt"
	"strconv"

	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var defaultMinCPUs int = 4
var defaultMinMemoryLimit float64 = 15
var defaultMinSSDStorage float64 = 50

// TODO: This needs to be pulled from config.
var ports = []string{"5432", "9000", "9090"}

// Preflight Interface created for Preflight check
// objects.
type Preflight interface {
	Name() string
	WarningLevel() string
	Execute()
}

var preflightCheckObjects map[string]Preflight = make(map[string]Preflight)

// PreflightList lists all preflight checks currently conducted.
func PreflightList() {
	log.Info("Preflight Check List:")
	for _, check := range preflightCheckObjects {
		log.Info(check.Name() + ": " + check.WarningLevel())
	}
}

func RegisterPreflightCheck(check Preflight) error {
	name := check.Name()
	if _, ok := preflightCheckObjects[name]; ok {
		return fmt.Errorf("preflight check '%s' already exists", name)
	}
	preflightCheckObjects[name] = check
	return nil
}

// Run conducts all preflight checks except for those specified to be skipped.
func Run(filename string, skipChecks ...string) {

	preflightChecksList := []string{}

	for _, check := range preflightCheckObjects {

		preflightChecksList = append(preflightChecksList, check.Name())

	}

	viper.SetConfigName(filename)
	viper.SetConfigType("yml")
	viper.AddConfigPath(".")
	err := viper.ReadInConfig()

	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	preflight := viper.Get("preflight").(map[string]interface{})

	overrideWarning, _ := strconv.ParseBool(fmt.Sprint(preflight["overridewarning"]))

	for _, check := range skipChecks {

		if !common.Contains(preflightChecksList, check) {

			log.Fatal(check + " is not a valid Preflight check! Please use the " +
				"Preflight list command to get all available Preflight checks.")
		}
	}

	// If the config entry has been set to a non true/false value, then we assume that the
	// user does not wish to override warning level preflight checks.

	// Otherwise, warning level checks can be overriden through a user config entry
	// if desired (overrideWarning = True)

	for _, check := range preflightCheckObjects {

		if common.Contains(skipChecks, check.Name()) {
			if check.WarningLevel() == "critical" {

				log.Fatal("The " + check.Name() + " preflight check is at a critical level " +
					"and cannot be skipped.")
			}
		}

		if !common.Contains(skipChecks, check.Name()) {
			if !(overrideWarning && check.WarningLevel() == "warning") {
				check.Execute()
			}
		}
	}

}
