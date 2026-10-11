// Copyright (c) YugabyteDB, Inc.

package util

import (
	"crypto/fips140"
	"fmt"
	"os"
)

// AllowNonFipsEnv lets a build that is not in FIPS mode start anyway. Only for local
// development: a released binary is built with GOFIPS140, so the check passing is what shows
// the validated module is in use.
const AllowNonFipsEnv = "YB_ALLOW_NON_FIPS"

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
