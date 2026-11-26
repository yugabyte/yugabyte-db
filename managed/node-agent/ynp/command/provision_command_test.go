// Copyright (c) YugabyteDB, Inc.

package command

import (
	"strings"
	"testing"
)

func TestScriptFailureReasonRunPhase(t *testing.T) {
	// Output shape produced by buildScript for the run phase: a failed module in the middle, a
	// module that succeeds after it, and the summary that echoes the failure line again.
	output := `Executing module ConfigureOs
+ sysctl -w vm.swappiness=0
vm.swappiness = 0
Executing module ConfigureFips
Kernel FIPS mode is off (/proc/sys/crypto/fips_enabled=0).
Automatic enablement is only supported on the RHEL family, and this node is amzn (Unknown). Provide an image that already has FIPS mode on.
Module ConfigureFips failed with code 1
Executing module ConfigureSystemd
Created symlink /etc/systemd/system/multi-user.target.wants/yb-master.service
Module ConfigureFips failed with code 1
`
	got := scriptFailureReason(output)
	want := "Module ConfigureFips failed with code 1: " +
		"Kernel FIPS mode is off (/proc/sys/crypto/fips_enabled=0). " +
		"Automatic enablement is only supported on the RHEL family, and this node is amzn " +
		"(Unknown). Provide an image that already has FIPS mode on."
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonTailIsBounded(t *testing.T) {
	lines := []string{"Executing module InstallPackages"}
	for i := 0; i < 20; i++ {
		lines = append(lines, "line"+string(rune('a'+i)))
	}
	lines = append(lines, "Module InstallPackages failed with code 1")
	got := scriptFailureReason(strings.Join(lines, "\n"))
	want := "Module InstallPackages failed with code 1: linep lineq liner lines linet"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonFatalModule(t *testing.T) {
	output := `Executing module MountEphemeralDrive
FATAL ERROR: Mount - /dev/xvdb is not a block device
Module MountEphemeralDrive failed with code 2
FATAL error in module MountEphemeralDrive. Aborting immediately.
`
	got := scriptFailureReason(output)
	want := "Module MountEphemeralDrive failed with code 2: " +
		"FATAL ERROR: Mount - /dev/xvdb is not a block device"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonPrecheckPhase(t *testing.T) {
	output := `Executing module ConfigureFips
{
"results":[
    {
      "check": "Swappiness",
      "result": "PASS",
      "message": "vm.swappiness is 0"
    },
    {
      "check": "FIPS Mode",
      "result": "FAIL",
      "message": "Kernel FIPS mode is off and no reboot is pending"
    }
]}
Pre-flight checks failed, Please fix them before continuing.
`
	got := scriptFailureReason(output)
	want := "FIPS Mode check FAIL: Kernel FIPS mode is off and no reboot is pending"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonPrecheckMessageWithQuotes(t *testing.T) {
	// add_result does not escape the message, so a quote inside it must not cut the message
	// short or hide the entry.
	output := `{
"results":[
    {
      "check": "Mount Points",
      "result": "FAIL",
      "message": "Mount point "/mnt/d0" is not writable"
    }
]}
`
	got := scriptFailureReason(output)
	want := `Mount Points check FAIL: Mount point "/mnt/d0" is not writable`
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonSilentModule(t *testing.T) {
	output := "Executing module ConfigureTHP\nModule ConfigureTHP failed with code 1\n"
	got := scriptFailureReason(output)
	want := "Module ConfigureTHP failed with code 1"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonSkipsTraceLines(t *testing.T) {
	// Under loglevel DEBUG the script runs with set -x, whose trace must not crowd out the
	// module's own output. The traced echo also repeats the failure line, which must not count
	// as a second failure.
	output := `+ echo 'Executing module ConfigureFips'
Executing module ConfigureFips
+ cat /proc/sys/crypto/fips_enabled
+ echo 'Kernel FIPS mode is off.'
Kernel FIPS mode is off.
+ exit_code=1
+ echo 'Module ConfigureFips failed with code 1'
Module ConfigureFips failed with code 1
`
	got := scriptFailureReason(output)
	want := "Module ConfigureFips failed with code 1: Kernel FIPS mode is off."
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonNoMarkersFallsBackToTail(t *testing.T) {
	// A failure before any module runs, e.g. bash refusing the script, has no markers; the
	// last lines are the only reason available.
	output := "export PATH\n/bin/bash: /tmp/tmp123_run: Permission denied\n"
	got := scriptFailureReason(output)
	want := "export PATH /bin/bash: /tmp/tmp123_run: Permission denied"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
	lines := []string{}
	for i := 0; i < 10; i++ {
		lines = append(lines, "line"+string(rune('a'+i)))
	}
	got = scriptFailureReason(strings.Join(lines, "\n"))
	if want := "linef lineg lineh linei linej"; got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestScriptFailureReasonEmptyOutput(t *testing.T) {
	for _, output := range []string{"", "\n\n", "+ set -x\n+ exit 1\n"} {
		if got := scriptFailureReason(output); got != "" {
			t.Fatalf("output %q: got %q, want empty", output, got)
		}
	}
}
