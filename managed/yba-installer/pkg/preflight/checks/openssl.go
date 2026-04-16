/*
 * Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Python checks to ensure the correct version of python exists
var OpenSSL = &opensslCheck{"openssl", false}

var openSSLRegex = regexp.MustCompile(`OpenSSL (\d+)\.(\d+)\.(\d+)`)

type opensslCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (p opensslCheck) Name() string {
	return p.name
}

// SkipAllowed gets if the check can be skipped
func (p opensslCheck) SkipAllowed() bool {
	return p.skipAllowed
}

// Execute runs the python check. Ensures we have a valid version of python for yba.
func (p opensslCheck) Execute() Result {
	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}
	// Need to check openssl version 1.1.1+ on Python 3.10+ because of https://docs.python.org/3/library/ssl.html#module-ssl
	op := shell.Run("openssl", "version")
	if !op.Succeeded() {
		log.Debug("need openssl installed on system")
		res.Error = fmt.Errorf("System does not have OpenSSL installed. Please install OpenSSL 1.1.1+.")
		res.Status = StatusCritical
		return res
	}
	opTrim := strings.TrimSpace(op.StdoutString())
	sslMatch := openSSLRegex.FindStringSubmatch(opTrim)
	if len(sslMatch) < 4 {
		log.Warn("could not parse openssl version " + opTrim)
		res.Status = StatusSkipped
		return res
	}
	major, _ := strconv.Atoi(sslMatch[1])
	minor, _ := strconv.Atoi(sslMatch[2])
	patch, _ := strconv.Atoi(sslMatch[3])
	if (major < 1) || (major == 1 && minor < 1) || (major == 1 && minor == 1 && patch < 1) {
		log.Debug("system does not meet openssl requirements: " + sslMatch[0])
		res.Error = fmt.Errorf("System has OpenSSL version %s, needs at least 1.1.1", sslMatch[0])
		res.Status = StatusCritical
		return res
	}
	return res
}
