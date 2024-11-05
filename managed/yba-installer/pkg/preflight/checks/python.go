/*
 * Copyright (c) YugaByte, Inc.
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
var Python = &pythonCheck{"python", false}

var pythonBinaryNames = []string{"python3.8", "python3.11", "python3.10", "python3.9", "python3"}

var pythonVersionRegex = regexp.MustCompile(`Python (\d+)\.(\d+)`)
var openSSLRegex = regexp.MustCompile(`OpenSSL (\d+)\.(\d+)\.(\d+)`)

type pythonCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (p pythonCheck) Name() string {
	return p.name
}

// SkipAllowed gets if the check can be skipped
func (p pythonCheck) SkipAllowed() bool {
	return p.skipAllowed
}

// Execute runs the python check. Ensures we have a valid version of python for yba.
func (p pythonCheck) Execute() Result {
	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}

	for _, binary := range pythonBinaryNames {
		log.Debug("checking for python binary " + binary)
		out := shell.Run(binary, "--version")
		if !out.Succeeded() {
			log.Debug("python binary " + binary + " failed, trying next")
			continue
		}
		outputTrimmed := strings.TrimSpace(out.StdoutString())
		match := pythonVersionRegex.FindStringSubmatch(outputTrimmed)
		if len(match) < 3 {
			continue
		}
		majorVersion, _ := strconv.Atoi(match[1])
		minorVersion, _ := strconv.Atoi(match[2])
		if majorVersion == 3 && minorVersion >= 8 && minorVersion <= 11 {
			log.Info("System meets Python installation requirements with version " + outputTrimmed)
			if minorVersion >= 10 {
				// Need to check openssl version 1.1.1+ on Python 3.10+ because of https://docs.python.org/3/library/ssl.html#module-ssl
				op := shell.Run("openssl", "version")
				if !out.Succeeded() {
					log.Debug("need openssl installed on system")
					res.Error = fmt.Errorf("System does not have OpenSSL installed. Please install OpenSSL 1.1.1+.")
					res.Status = StatusCritical
					return res
				}
				opTrim := strings.TrimSpace(op.StdoutString())
				sslMatch := openSSLRegex.FindStringSubmatch(opTrim)
				if len(sslMatch) < 4 {
					log.Warn("could not validate openssl version, skipping check")
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
			}
		}
		return res
	}

	res.Error = fmt.Errorf("System does not meet Python requirements. Please install any " +
		"version of Python between 3.8 and 3.11.")
	res.Status = StatusCritical
	return res
}
