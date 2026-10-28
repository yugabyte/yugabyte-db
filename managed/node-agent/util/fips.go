// Copyright (c) YugabyteDB, Inc.

package util

import (
	"crypto/fips140"
	"fmt"
	"os"
	"strings"
	"syscall"

	"github.com/spf13/pflag"
)

// AllowNonFipsEnv lets a build that is not in FIPS mode start anyway. Only for local
// development: a released binary is built with GOFIPS140, so the check passing is what shows
// the validated module is in use.
const AllowNonFipsEnv = "YB_ALLOW_NON_FIPS"

// fipsReexecEnv marks the process ApplyFipsMode started, so it does not re-execute again.
const fipsReexecEnv = "YB_FIPS_REEXEC"

// VerifyFipsMode stops the process unless the FIPS 140-3 validated Go module is in use.
//
// Binaries are built with GOFIPS140, which links the validated module and defaults them to
// GODEBUG=fips140=on - but GODEBUG is read from the environment at startup, so fips140=off still
// turns it back off at run time. This is the check that a deployment expecting FIPS gets it.
func VerifyFipsMode() {
	if fips140.Enabled() {
		return
	}
	msg := "FIPS 140-3 mode is not active: the binary was either built without GOFIPS140 or " +
		"started with GODEBUG=fips140=off"
	if os.Getenv(AllowNonFipsEnv) == "1" {
		fmt.Fprintf(
			os.Stderr,
			"WARNING: %s. Continuing because %s=1 is set.\n",
			msg,
			AllowNonFipsEnv,
		)
		return
	}
	fmt.Fprintf(os.Stderr, "ERROR: %s. Set %s=1 to run anyway.\n", msg, AllowNonFipsEnv)
	os.Exit(1)
}

// ApplyFipsMode runs the process in FIPS mode only when the YBA it belongs to is FIPS enabled.
// FIPS mode also restricts crypto/tls to an allow-list (RSA 2048/3072/4096 or ECDSA P-256/P-384
// certs, no SHA-1) that rejects certs a non-FIPS YBA accepts. The mode is fixed at process start,
// so a mismatch re-executes the binary with GODEBUG fips140 set. Child processes inherit that
// GODEBUG and apply their own mode the same way.
func ApplyFipsMode(fipsEnabled bool) {
	reexecuted := os.Getenv(fipsReexecEnv) != ""
	os.Unsetenv(fipsReexecEnv)
	if fips140.Enabled() != fipsEnabled {
		if reexecuted {
			fmt.Fprintf(os.Stderr, "WARNING: FIPS mode is %t after restarting with FIPS mode %t\n",
				fips140.Enabled(), fipsEnabled)
		} else {
			exe, err := os.Executable()
			if err == nil {
				err = syscall.Exec(exe, os.Args, fipsEnviron(os.Environ(), fipsEnabled))
			}
			fmt.Fprintf(os.Stderr, "WARNING: could not restart with FIPS mode %t: %v\n", fipsEnabled, err)
		}
	}
	if fipsEnabled {
		VerifyFipsMode()
	}
}

// fipsEnviron returns environ with GODEBUG fips140 set to the given mode, keeping its other
// settings.
func fipsEnviron(environ []string, fipsEnabled bool) []string {
	mode := "fips140=off"
	if fipsEnabled {
		mode = "fips140=on"
	}
	settings := []string{}
	result := []string{fipsReexecEnv + "=1"}
	for _, kv := range environ {
		value, isGodebug := strings.CutPrefix(kv, "GODEBUG=")
		if !isGodebug {
			result = append(result, kv)
			continue
		}
		for _, setting := range strings.Split(value, ",") {
			setting = strings.TrimSpace(setting)
			if setting != "" && !strings.HasPrefix(setting, "fips140=") {
				settings = append(settings, setting)
			}
		}
	}
	return append(result, "GODEBUG="+strings.Join(append(settings, mode), ","))
}

// NodeAgentFipsEnabled reports whether node agent belongs to a FIPS enabled YBA: --fips_enabled
// when the command sets it, as configure and register do before storing it, otherwise the stored
// value.
func NodeAgentFipsEnabled(flags *pflag.FlagSet) bool {
	if flag := flags.Lookup("fips_enabled"); flag != nil && flag.Changed {
		enabled, _ := flags.GetBool("fips_enabled")
		return enabled
	}
	return CurrentConfig().Bool(PlatformFipsEnabledKey)
}
