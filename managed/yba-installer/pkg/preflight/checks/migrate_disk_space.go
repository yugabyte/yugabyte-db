package checks

import (
	"fmt"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

var MigrateDiskCheck = &migrateDistCheck{"replicated-migration-disk-space", true}

type migrateDistCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (mdc migrateDistCheck) Name() string {
	return mdc.name
}

// SkipAllowed returns if we are allowed to skip the check
func (mdc migrateDistCheck) SkipAllowed() bool {
	return mdc.skipAllowed
}

// Execute will run the root check.
// Validates the root install directory is not used and has enough free space??
func (mdc migrateDistCheck) Execute() Result {
	res := Result{
		Check:  mdc.name,
		Status: StatusPassed,
	}

	baseDir := common.GetBaseInstall()
	bytesAvail, err := getFreeBytes(baseDir)
	if err != nil {
		res.Error = err
		res.Status = StatusCritical
		return res
	}

	bytesNeeded, err := calculateBytesNeeded()
	if err != nil {
		res.Error = err
		res.Status = StatusCritical
		return res
	}
	if bytesNeeded >= bytesAvail {
		res.Error = fmt.Errorf(
			"Availabile disk space on volume %s is less than minimum required %s",
			humanize.IBytes(bytesAvail), humanize.IBytes(bytesNeeded))
		res.Status = StatusCritical
		return res
	}
	return res
}

func calculateBytesNeeded() (uint64, error) {
	var total uint64 = 0

	// Size of a normal yba-installer install - 5GB.
	total += 5 * 1024 * 1024 * 1024

	// Get the size of the postgres backup
	pgBytes, err := getReplicatedPGSize()
	if err != nil {
		return 0, fmt.Errorf("failed to get size of postgres: %w", err)
	}
	logging.DebugLF(fmt.Sprintf("found pg bytes: %d", pgBytes))
	total += pgBytes

	// keep 10% extra space overhead
	total += total / 10
	return total, nil
}

func getReplicatedPGSize() (uint64, error) {
	psqlCmd := []string{
		"psql",
		"-h", "localhost",
		"-U", "postgres",
		"-d", "yugaware",
		"--quiet",
		"--tuples-only",
		`--command=SELECT pg_size_pretty( pg_database_size('yugaware') );`,
	}
	args := []string{"exec", "postgres"}
	args = append(args, psqlCmd...)
	out := shell.Run("docker", args...)
	if !out.SucceededOrLog() {
		return 0, fmt.Errorf("failed to get yugaware database size: %w", out.Error)
	}
	humanSize := strings.TrimSpace(out.StdoutString())
	return humanize.ParseBytes(humanSize)
}
