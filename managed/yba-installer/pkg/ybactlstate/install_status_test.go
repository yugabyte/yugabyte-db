package ybactlstate

import (
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"testing"
)

type testJsonStruct struct {
	Status status `json:"status"`
}

func TestMarshalling(t *testing.T) {
	tests := []struct {
		Input        int
		ResultString string
		ResultError  error
	}{
		{1, `{"status":"Installed"}`, nil},
		{2, `{"status":"Installing"}`, nil},
		{3, `{"status":"Upgrading"}`, nil},
		{4, `{"status":"Soft Cleaned"}`, nil},
		{5, `{"status":"Uninstalled"}`, nil},
		{0, "", InvalidStatusError},
		{999, "", InvalidStatusError},
	}

	for ii, test := range tests {
		t.Run(fmt.Sprintf("%d_Marshalling", ii), func(t *testing.T) {
			jt := testJsonStruct{
				Status: status(test.Input),
			}
			b, err := json.Marshal(jt)
			// Validate expected nil error:
			if test.ResultError == nil {
				if err != nil {
					t.Error("expected no error, got " + err.Error())
				}
				if string(b) != test.ResultString {
					t.Errorf("got result '%s' - expected '%s'", string(b), test.ResultString)
				}
			} else {
				if !errors.Is(err, test.ResultError) {
					t.Errorf("expected err %s - got %s", test.ResultError, err)
				}
			}
		})
	}
}

func TestUnmarshalling(t *testing.T) {
	tests := []struct {
		Input       []byte
		Result      testJsonStruct
		ResultError error
	}{
		{[]byte(`{"status":"Installed"}`), testJsonStruct{InstalledStatus}, nil},
		{[]byte(`{"status":"Installing"}`), testJsonStruct{InstallingStatus}, nil},
		{[]byte(`{"status":"Upgrading"}`), testJsonStruct{UpgradingStatus}, nil},
		{[]byte(`{"status":"Soft Cleaned"}`), testJsonStruct{SoftCleanStatus}, nil},
		{[]byte(`{"status":"Uninstalled"}`), testJsonStruct{UninstalledStatus}, nil},
		{[]byte(`{"status":"jiberish"}`), testJsonStruct{}, InvalidStatusError},
	}
	for ii, test := range tests {
		t.Run(fmt.Sprintf("%d_Unmarshalling", ii), func(t *testing.T) {
			var target testJsonStruct
			err := json.Unmarshal(test.Input, &target)
			if test.ResultError == nil {
				if err != nil {
					t.Errorf("unexpected error %s", err)
				}
				if target.Status != test.Result.Status {
					t.Errorf("unexpected status %s - expected %s", target.Status.String(),
						test.Result.Status.String())
				}
			} else {
				if !errors.Is(err, test.ResultError) {
					t.Errorf("unexpected error %s - expected %s", err, test.ResultError)
				}
			}
		})
	}
}

// This test validates that all status enums are defined correctly - both with a valid String and
// toStatus implementation
func TestStatusEnum(t *testing.T) {
	for i := NoStatus; i < endStatus; i++ {
		str := i.String()
		if strings.Contains(str, "unknown") {
			t.Errorf("found unknown status - %s: %s", i, str)
		}
		if _, ok := toStatus[str]; !ok {
			t.Errorf("cannot convert string '%s' to status", str)
		}
	}
}

// Test common state transitions
func TestTransitions(t *testing.T) {
	tests := []struct {
		Begin, End status
		Succeed    bool
		Name       string
	}{
		{UninstalledStatus, InstallingStatus, true, "Install"},
		{UninstalledStatus, UpgradingStatus, false, "Upgrade from uninstalled"},
		{UninstalledStatus, MigratingStatus, true, "Start replicated migration"},
		{InstallingStatus, InstalledStatus, true, "Finish install"},
		{InstallingStatus, CleaningStatus, true, "clean from installing"},
		{InstallingStatus, UpgradingStatus, false, "block upgrade from installing"},
		{InstalledStatus, UpgradingStatus, true, "upgrade"},
		{InstalledStatus, MigrateStatus, false, "block migrate on existing install"},
		{InstalledStatus, InstallingStatus, false, "block reinstall"},
		{UpgradingStatus, InstalledStatus, true, "finish upgrade"},
		{UpgradingStatus, CleaningStatus, true, "clean from upgrading"},
		{CleaningStatus, SoftCleanStatus, true, "soft clean"},
		{CleaningStatus, InstalledStatus, false, "clean must finish"},
		{MigratingStatus, MigrateStatus, true, "migrate start"},
		{MigratingStatus, RollbackStatus, true, "rollback failed migrate"},
		{MigratingStatus, CleaningStatus, false, "migrating must rollback"},
		{MigrateStatus, RollbackStatus, true, "rollback from migrate"},
		{MigrateStatus, FinishingStatus, true, "finish migration"},
		{MigrateStatus, CleaningStatus, false, "migrate must rollback"},
		{FinishingStatus, InstalledStatus, true, "finishing to installed"},
		{FinishingStatus, RollbackStatus, false, "finish must complete"},
	}
	for ii, test := range tests {
		t.Run(
			fmt.Sprintf("%s:%s-%d", t.Name(), test.Name, ii),
			func(t *testing.T) {
				result := test.Begin.TransitionValid(test.End)
				if result != test.Succeed {
					t.Errorf("expected %v, got %v", test.Succeed, result)
				}
			})
	}
}
