package ybactlstate

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"strconv"
)

var InvalidStatusError error = errors.New("invalid status")

type status int

const (
	NoStatus          status = iota // When a previous version of state was not aware of status
	InstalledStatus                 // YBA is installed
	InstallingStatus                // Install is in progress
	UpgradingStatus                 // Upgrade is in progress
	CleaningStatus                  // In the process of running clean
	SoftCleanStatus                 // A soft clean has been performed (data still remains)
	UninstalledStatus               // No yba software or data is installed
	MigratingStatus									// Migration is in progress
	endStatus                       // Not a real status, used for validation
)

var toStatus = map[string]status{
	"Installed":    InstalledStatus,
	"Installing":   InstallingStatus,
	"Upgrading":    UpgradingStatus,
	"Cleaning":     CleaningStatus,
	"Soft Cleaned": SoftCleanStatus,
	"Uninstalled":  UninstalledStatus,
	"Migrating": 		MigratingStatus,
}

// String value of the status
func (s status) String() string {
	switch s {
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
	default:
		return "unknown status " + strconv.Itoa(int(s))
	}
}

// Validate this is a known status.
func (s status) Validate() bool {
	return s > 0 && s < endStatus
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
	case NoStatus:
		return next == UpgradingStatus || next == CleaningStatus
	case InstalledStatus:
		return next == UpgradingStatus || next == CleaningStatus
	case InstallingStatus:
		return next == InstalledStatus || next == CleaningStatus || next == UninstalledStatus
	case UpgradingStatus:
		return next == InstalledStatus || next == CleaningStatus || next == UninstalledStatus
	case CleaningStatus:
		return next == SoftCleanStatus
	case SoftCleanStatus:
		return next == InstallingStatus
	case UninstalledStatus:
		return next == InstallingStatus
	default:
		return false
	}
}
