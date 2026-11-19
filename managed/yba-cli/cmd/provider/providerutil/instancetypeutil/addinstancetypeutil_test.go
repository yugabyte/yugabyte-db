/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

import (
	"os"
	"os/exec"
	"strings"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

const volumeFatalEnv = "TEST_VOLUME_UNKNOWN_KEY"

func TestBuildVolumeDetailsRejectsUnknownKey(t *testing.T) {
	// UnknownKeyError exits the process, so the typo runs in a child.
	if os.Getenv(volumeFatalEnv) != "" {
		buildVolumeDetails([]string{"mount-points=/mnt/d0::size_=200"}, util.AWSProviderType)
		return
	}

	got := buildVolumeDetails(
		[]string{"mount-points=/mnt/d0::size=200::type=SSD"}, util.AWSProviderType)
	if len(got) != 1 || got[0].VolumeSizeGB != 200 || got[0].VolumeType != "SSD" {
		t.Fatalf("volume = %+v, want size 200 and type SSD", got)
	}

	cmd := exec.Command(os.Args[0], "-test.run=^TestBuildVolumeDetailsRejectsUnknownKey$")
	cmd.Env = append(os.Environ(), volumeFatalEnv+"=1")
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("expected the parser to exit with an error, output %s", out)
	}
	// logrus quotes msg when stderr is not a terminal.
	if want := `Unknown key \"size_\" in volume description`; !strings.Contains(string(out), want) {
		t.Fatalf("output %q, want %q", out, want)
	}
}
