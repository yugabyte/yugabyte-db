/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"fmt"
	"os"
	"text/tabwriter"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/preflight/checks"
)

// Check Interface created for Check check
// objects.
type Check interface {
	Name() string
	SkipAllowed() bool
	Execute() checks.Result
}

// Run conducts all preflight checks except for those specified to be skipped.
//
// We will run all specified preflight checks and return errors for those that failed.
// If an empty array is returned, there were no failures found.
//
func Run(checkList []Check, skipChecks ...string) []checks.Result {
	// Preallocate underlying arrary
	var results []checks.Result = make([]checks.Result, 0, len(checkList))

	for _, check := range checkList {
		// Skip the check if needed
		if common.Contains(skipChecks, check.Name()) {
			if !check.SkipAllowed() {
				log.Fatal("The preflight check '" + check.Name() + "' is at a critical level " +
					"and cannot be skipped.")
			}
			log.Debug("skipping preflight check '" + check.Name() + "'")
			continue
		}

		log.Info("Running preflight check '" + check.Name() + "'")
		result := check.Execute()
		if result.Error != nil {
			// If the user wants to allow warnings, log warning and continue
			if result.Status == checks.StatusWarning {
				log.Warn(check.Name() + " raised a warning: " + result.Error.Error())
			} else {
				log.Info("preflight " + check.Name() + " failed: " + result.Error.Error())
				results = append(results, result)
			}
		}
	}
	return results
}

// PrintPreflightResults will print all preflight errors to stdout, for the
// user to see
func PrintPreflightResults(results []checks.Result) {
	preflightWriter := tabwriter.NewWriter(os.Stdout, 3, 0, 1, ' ', 0)
	fmt.Println("Preflight errors:")
	fmt.Fprintln(preflightWriter, "#\tPreflight Check\tStatus\tError")
	for ii, result := range results {
		fmt.Fprintf(preflightWriter, "%d\t%s\t%s\t%s\n",
			ii+1,
			result.Check,
			result.Status.String(),
			result.Error)
	}
	preflightWriter.Flush()
}

// ShouldFail checks the list of results for critical failures. If any are found,
// return true (we should fail).
// Warning status should not cause a fail.
func ShouldFail(results []checks.Result) bool {
	for _, result := range results {
		if result.Status == checks.StatusCritical {
			return true
		}
	}
	return false
}
