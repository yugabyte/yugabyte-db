package ybactlstate

import (
	"bytes"
	"fmt"
	"testing"
)

func TestTransitionStatus(t *testing.T) {
	mockFS, deferFunc := patchFS()
	defer deferFunc()

	tests := []struct {
		Succeeded     bool   // If the TransitionStatus is suppose to succeed
		CurrentStatus status // CurrentStatus to set to the state
		NextStatus    status // Status to pass to TransitionStatus
		StoreErr      error  // Error to return from StoreState
	}{
		{true, UninstalledStatus, InstallingStatus, nil},                 // success test
		{false, UninstalledStatus, UpgradingStatus, nil},                 // invalid transition
		{false, UninstalledStatus, InstallingStatus, fmt.Errorf("fail")}, // Store fails
	}
	for ii, test := range tests {
		t.Run(fmt.Sprintf("%s-%d", t.Name(), ii),
			func(t *testing.T) {
				mockFS.CreateErr = test.StoreErr
				mockFS.CreateBuffer = new(bytes.Buffer)
				s := &State{}
				s.CurrentStatus = test.CurrentStatus
				err := s.TransitionStatus(test.NextStatus)
				if test.Succeeded && err != nil {
					t.Errorf("expected success, instead got error %s: %v", err.Error(), test)
				} else if !test.Succeeded && err == nil {
					t.Errorf("expected a failure, instead transition succeeded: %v", test)
				}
			})
	}

}
