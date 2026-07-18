package checks

import (
	"fmt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/yugaware"
)

var NonRootUpgradeCheck = &nonRootUpgradeCheck{"non-root supported upgrade", true}

type nonRootUpgradeCheck struct {
	name        string
	skipAllowed bool
}

func (u nonRootUpgradeCheck) Name() string { return u.name }

func (u nonRootUpgradeCheck) SkipAllowed() bool { return u.skipAllowed }

func (u nonRootUpgradeCheck) Execute() Result {
	res := Result{
		Check:  u.name,
		Status: StatusPassed,
	}
	if !common.HasSudoAccess() {
		installedVersion, err := yugaware.InstalledVersionFromMetadata()
		if err != nil {
			res.Status = StatusCritical
			res.Error = fmt.Errorf("could not determine initial version: %w", err)
			return res
		}
		version, err := common.NewYBVersion(installedVersion)
		if err != nil {
			res.Status = StatusCritical
			res.Error = fmt.Errorf("could not determine new version: %w", err)
			return res
		}
		// Stable versions less then 2024.2 and preview versions less then 2.23 should not upgrade.
		if version.IsStable {
			if common.LessVersions(installedVersion, "2024.2.0.0") {
				res.Status = StatusCritical
				res.Error = fmt.Errorf("version %s cannot be upgraded in non-root mode", installedVersion)
			}
		} else {
			if common.LessVersions(installedVersion, "2.23.0.0") {
				res.Status = StatusCritical
				res.Error = fmt.Errorf("version %s cannot be upgraded in non-root mode", installedVersion)
			}
		}
	} else {
		res.Status = StatusSkipped
	}
	return res
}
