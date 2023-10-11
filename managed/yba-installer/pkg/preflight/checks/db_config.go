package checks

import (
	"fmt"
	"os"

	"github.com/spf13/viper"
)

var DBConfigCheck = &dbConfigCheck{
	"db-config",
	true,
}

type dbConfigCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (db dbConfigCheck) Name() string {
	return db.name
}

// SkipAllowed returns if the check can be skipped
func (db dbConfigCheck) SkipAllowed() bool {
	return db.skipAllowed
}

// Executes validates the db confifguration in yba-ctl.yml
func (db dbConfigCheck) Execute() Result {
	res := Result{
		Check:  db.name,
		Status: StatusPassed,
	}

	devModeEnabled := os.Getenv("YBA_MODE") == "dev"
	useExistingPostgres := viper.GetBool("postgres.useExisting.enabled")
	installPostgres := viper.GetBool("postgres.install.enabled")
	if (useExistingPostgres && installPostgres) ||
		(!devModeEnabled && !useExistingPostgres && !installPostgres) {
		res.Status = StatusCritical
		res.Error = fmt.Errorf(
			"exactly one of postgres.useExisting.enabled and " +
				"postgres.install.enabled should be set to true")
		return res
	}
	return res
}
