/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"fmt"

	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// PreflightCheck Interface created for PreflightCheck check
// objects.
type PreflightCheck interface {
	Name() string
	WarningLevel() string
	Execute() error
}

// Run conducts all preflight checks except for those specified to be skipped.
//
// We will run all specified preflight checks and return errors for those that failed.
// If an empty array is returned, there were no failures found.
//
// If preflight.overrideWarning is true, log the warning, but do not save the error.
func Run(checks []PreflightCheck, skipChecks ...string) []error {
	var errors []error
	for _, check := range checks {
		// Skip the check if needed
		if common.Contains(skipChecks, check.Name()) {
			if check.WarningLevel() == "critical" {
				log.Fatal("The preflight check '" + check.Name() + "' is at a critical level " +
					"and cannot be skipped.")
			}
			log.Debug("skipping preflight check '" + check.Name() + "'")
			continue
		}

		log.Info("Running preflight check '" + check.Name() + "'")
		err := check.Execute()
		if err != nil {
			// If the user wants to allow warnings, log warning and continue
			if viper.GetBool("preflight.overrideWarning") && check.WarningLevel() == "warning" {
				log.Warn(check.Name() + " failed. Overriding warning " + err.Error())
			} else {
				log.Info("preflight " + check.Name() + " failed. marking failed for later")
				errors = append(errors, err)
			}
		}
	}
	return errors
}

// PrintPreflightErrors will print all preflight errors to stdout, for the
// user to see
func PrintPreflightErrors(errs []error) {
	fmt.Println("Preflight errors:")
	for ii, err := range errs {
		fmt.Printf("%d: %v", ii+1, err)
	}
}
