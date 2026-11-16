/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestCheckPauseResumeState(t *testing.T) {
	cases := []struct {
		name       string
		paused     bool
		inProgress bool
		pause      bool
		wantErr    bool
	}{
		{"pause ready", false, false, true, false},
		{"pause paused", true, false, true, true},
		{"pause task running", false, true, true, true},
		{"resume paused", true, false, false, false},
		{"resume ready", false, false, false, true},
		{"resume task running", true, true, false, true},
	}
	for _, c := range cases {
		details := ybaclient.UniverseDefinitionTaskParamsResp{
			UniversePaused:   util.GetBoolPointer(c.paused),
			UpdateInProgress: util.GetBoolPointer(c.inProgress),
		}
		universe := ybaclient.UniverseResp{
			Name:            util.GetStringPointer("u1"),
			UniverseDetails: &details,
		}
		err := checkPauseResumeState(universe, c.pause)
		if (err != nil) != c.wantErr {
			t.Errorf("%s: got error %v, want error %v", c.name, err, c.wantErr)
		}
	}
}
