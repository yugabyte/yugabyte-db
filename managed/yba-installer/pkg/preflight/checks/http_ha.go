package checks

import (
	"fmt"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
)

var HTTPHACheck = &httpHACheck{"http-ha", true}

type httpHACheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (hhc httpHACheck) Name() string {
	return hhc.name
}

// SkipAllowed returns if we are allowed to skip the check
func (hhc httpHACheck) SkipAllowed() bool {
	return hhc.skipAllowed
}

// Execute will check whether or not HA is enabled in an HTTP Replicated
func (hhc httpHACheck) Execute() Result {
	res := Result{
		Check:  hhc.name,
		Status: StatusPassed,
	}

	// Dump replicated settings
	replCtl := replicatedctl.New(replicatedctl.Config{})
	config, err := replCtl.AppConfigView()
	if err != nil {
		res.Error = fmt.Errorf("failed to export replicated app config: %w", err)
		res.Status = StatusCritical
		return res
	}

	var https bool
	// If there is no https entry, or it is not parsable, assume http.
	isHTTPSEntry, err := config.Get("https_enabled")
	if err != nil {
		https = false
	} else {
		https = isHTTPSEntry.Get() == "1"
	}

	// HTTPS HA can be migrated automatically
	if https {
		return res
	}
	// In case of HTTP we need to check PG for HA config
	cmdArgs := "exec -i postgres sh -c 'psql -d yugaware -U postgres -t -c \"SELECT COUNT(*) FROM high_availability_config;\"' | tr -d '[:space:]'"
	out := shell.RunShell("docker", cmdArgs)
	if !out.SucceededOrLog() {
		res.Error = fmt.Errorf("error querying for HA config")
		res.Status = StatusCritical
		return res
	}
	if strings.ReplaceAll(out.StdoutString(), " ", "") != "0" {
		res.Error = fmt.Errorf("found HA config with HTTP, please delete before proceeding with migration")
		res.Status = StatusCritical
		return res
	}
	return res
}
