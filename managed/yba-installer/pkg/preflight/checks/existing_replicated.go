package checks

import (
	"fmt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
)

var ReplicatedNotExists = replicatedNotExistsCheck{"replicated-exists", true}

type replicatedNotExistsCheck struct {
	name				string
	skipAllowed bool
}

// Name gets the check name.
func (r replicatedNotExistsCheck) Name() string {
	return r.name
}

// WarningLevel gets the check's warning level.
func (r replicatedNotExistsCheck) SkipAllowed() bool {
	return r.skipAllowed
}

func (r replicatedNotExistsCheck) Execute() Result {
	res := Result{
		Check: r.name,
		Status: StatusPassed,
	}

	// Dump replicated settings
	replCtl := replicatedctl.New(replicatedctl.Config{})
	_, err := replCtl.AppConfigExport()
	if err == nil {
		err := fmt.Errorf("found replicated install, use replicated-migrate instead")
		res.Error = err
		res.Status = StatusCritical
	}
	return res
}