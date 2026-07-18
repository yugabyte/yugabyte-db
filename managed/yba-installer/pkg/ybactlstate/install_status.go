package ybactlstate

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"strconv"
)

/* StateWorkflows:
**Install**
Uninstall -> installing -> installed
               failure \_> cleaning

**Upgrade**
Legacy yba-ctl upgrade starts at no status. Typical upgrades start at Installed
NoStatus  \
          -> Upgrading -> Installed
Installed /   failure \_> Cleaning

**Replicated Migrating**pkg/ybactlstate/install_status.go
Uninstall -> Migrating -> Migrate -> Finishing -> Installed
                    |           |     failure \_> Finishing (Retry)
										|	          \_> Rollback (Customer wants back to replicated)
										\_> Rollback (Failure)
Rollback -> Uninstalled (Rollback leads to an uninstall of yba-installer, Replicated still exists)
*/

// InvalidStatusError for unparsable status
var InvalidStatusError error = errors.New("invalid status")

// StatusTransitionError when it is not possible from move from one status to the next
var StatusTransitionError error = errors.New("invalid status transition")

type status int

const (
	NoStatus          status = iota // When a previous version of state was not aware of status
	InstalledStatus                 // YBA is installed
	InstallingStatus                // Install is in progress
	UpgradingStatus                 // Upgrade is in progress
	CleaningStatus                  // In the process of running clean
	SoftCleanStatus                 // A soft clean has been performed (data still remains)
	UninstalledStatus               // No yba software or data is installed
	MigratingStatus                 // Begin replication migration
	MigrateStatus                   // Migration has begun, but finish not yet called.
	RollbackStatus                  // Rollback before finishing a migration
	FinishingStatus                 // Finish the migration
	endStatus                       // Not a real status, used for validation
)

var toStatus = map[string]status{
	"NoStatus":     NoStatus,
	"Installed":    InstalledStatus,
	"Installing":   InstallingStatus,
	"Upgrading":    UpgradingStatus,
	"Cleaning":     CleaningStatus,
	"Soft Cleaned": SoftCleanStatus,
	"Uninstalled":  UninstalledStatus,
	"Migrating":    MigratingStatus,
	"Migrate":      MigrateStatus,
	"Rollback":     RollbackStatus,
	"Finishing":    FinishingStatus,
}

// String value of the status
func (s status) String() string {
	switch s {
	case NoStatus:
		return "NoStatus"
	case InstalledStatus:
		return "Installed"
	case InstallingStatus:
		return "Installing"
	case UpgradingStatus:
		return "Upgrading"
	case CleaningStatus:
		return "Cleaning"
	case SoftCleanStatus:
		return "Soft Cleaned"
	case UninstalledStatus:
		return "Uninstalled"
	case MigratingStatus:
		return "Migrating"
	case MigrateStatus:
		return "Migrate"
	case RollbackStatus:
		return "Rollback"
	case FinishingStatus:
		return "Finishing"
	default:
		return "unknown status " + strconv.Itoa(int(s))
	}
}

// Validate this is a known status.
func (s status) Validate() bool {
	return s >= NoStatus && s < endStatus
}

func (s status) MarshalJSON() ([]byte, error) {
	if !s.Validate() {
		return []byte{}, fmt.Errorf("%w: %s", InvalidStatusError, s.String())
	}
	buf := bytes.NewBufferString(`"`)
	buf.WriteString(s.String())
	buf.WriteString(`"`)
	return buf.Bytes(), nil
}

func (s *status) UnmarshalJSON(b []byte) error {
	var t string
	err := json.Unmarshal(b, &t)
	if err != nil {
		return err
	}

	tempS, ok := toStatus[t]
	if !ok {
		return fmt.Errorf("%w: unknown status %s", InvalidStatusError, t)
	}
	*s = tempS
	return nil
}

// TransitionValid checks if the next status is valid from the current status
func (s status) TransitionValid(next status) bool {
	switch s {
	// NoStatus is only for cases of upgrading from ybactl that did not yet have state implemented
	case NoStatus:
		return next == UpgradingStatus || next == CleaningStatus
	case InstalledStatus:
		return next == UpgradingStatus || next == CleaningStatus
	case InstallingStatus:
		return next == InstalledStatus || next == CleaningStatus || next == InstallingStatus
	case UpgradingStatus:
		return next == InstalledStatus || next == CleaningStatus || next == UpgradingStatus
	case CleaningStatus:
		return next == SoftCleanStatus || next == CleaningStatus
	case SoftCleanStatus:
		return next == InstallingStatus
	case UninstalledStatus:
		return next == InstallingStatus || next == MigratingStatus || next == CleaningStatus
	case MigratingStatus:
		return next == RollbackStatus || next == MigrateStatus || next == MigratingStatus
	case MigrateStatus:
		return next == RollbackStatus || next == FinishingStatus
	// RollbackStatus should do a clean on its on. Failure here should either be another rollback
	// or support call.
	case RollbackStatus:
		return next == RollbackStatus || next == CleaningStatus
	case FinishingStatus:
		return next == InstalledStatus || next == FinishingStatus
	default:
		return false
	}
}
