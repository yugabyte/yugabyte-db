/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

const (
	manifestFileFlag = "manifest-file"

	// manifestPathInDevops is where the shipped manifest lives inside the devops directory.
	manifestPathInDevops = "bin/support_bundlev2_manifest.yaml"

	// ybaConfFileEnvVar points at the YBA application.conf when it is not in a standard location.
	ybaConfFileEnvVar = "YBA_APPLICATION_CONF"
)

// devopsHomeEnvVars carry the devops directory in dev (sbt) and release environments.
var devopsHomeEnvVars = []string{"DEVOPS_HOME", "yb_devops_home"}

// ybaConfFilePaths are the application.conf locations of the supported YBA install flavours:
// yba-installer, replicated/docker, and kubernetes respectively.
var ybaConfFilePaths = []string{
	"/opt/yugabyte/software/active/yb-platform/conf/yb-platform.conf",
	"/opt/yugabyte/yb-platform/conf/yb-platform.conf",
	"/opt/yugabyte/yugaware/conf/application.docker.conf",
	"/data/application.docker.conf",
}

// devopsHomeConfPattern matches the forms YBA writes: "yb.devops.home = <value>" (helm) and
// "devops.home = <value>" inside a yb { } block (yba-installer), quoted or not. It is a line match,
// not a HOCON parse, so an include, a nested devops { } block or a quoted key is unrecognised -
// which yields no path and falls through to the next candidate, never the wrong manifest.
var devopsHomeConfPattern = regexp.MustCompile(
	`(?:^|[\s{])(?:yb\.)?devops\.home\s*=\s*(?:"([^"]*)"|([^\s{}#,]+))`)

// resolveManifestFile returns the manifest to run with. An explicit --manifest-file wins; otherwise
// the manifest shipped under the devops directory is used, locating devops through DEVOPS_HOME or
// the yb.devops.home key of the YBA application.conf. This only resolves when the CLI runs on the
// YBA host, so pass --manifest-file when running elsewhere.
func resolveManifestFile(cmd *cobra.Command) (string, error) {
	if cmd.Flags().Changed(manifestFileFlag) {
		manifestFile := strings.TrimSpace(util.MustGetFlagString(cmd, manifestFileFlag))
		if _, err := os.Stat(manifestFile); err != nil {
			return "", fmt.Errorf("unable to read --%s %s: %w", manifestFileFlag, manifestFile, err)
		}
		return manifestFile, nil
	}

	var candidates []string
	found := forEachManifestCandidate(func(candidate string) bool {
		candidates = append(candidates, candidate)
		_, err := os.Stat(candidate)
		return err == nil
	})
	if !util.IsEmptyString(found) {
		return found, nil
	}
	return "", fmt.Errorf(
		"unable to locate the default support bundle v2 manifest: pass --%s, or set %s, or set "+
			"yb.devops.home in the YBA application.conf (%s); tried %s",
		manifestFileFlag,
		strings.Join(devopsHomeEnvVars, " or "),
		strings.Join(ybaConfFileCandidates(), ", "),
		strings.Join(candidates, ", "),
	)
}

// forEachManifestCandidate walks the manifest paths most authoritative first and returns the first
// one visit accepts. Lazy because reading the YBA application.conf line-scans up to four files,
// wasted when DEVOPS_HOME answers. Every candidate is anchored to the environment, the YBA
// configuration or the binary's directory - never the working directory a run started from.
func forEachManifestCandidate(visit func(candidate string) bool) string {
	seen := make(map[string]struct{})
	tryDevopsHome := func(devopsHome string) string {
		if util.IsEmptyString(devopsHome) {
			return ""
		}
		candidate := filepath.Join(strings.TrimSpace(devopsHome), manifestPathInDevops)
		if _, exists := seen[candidate]; exists {
			return ""
		}
		seen[candidate] = struct{}{}
		if visit(candidate) {
			return candidate
		}
		return ""
	}

	for _, envVar := range devopsHomeEnvVars {
		if found := tryDevopsHome(os.Getenv(envVar)); !util.IsEmptyString(found) {
			return found
		}
	}
	for _, confPath := range ybaConfFileCandidates() {
		if found := tryDevopsHome(devopsHomeFromYbaConf(confPath)); !util.IsEmptyString(found) {
			return found
		}
	}

	// Source and dev layouts, where devops sits next to the yba binary.
	if executable, err := os.Executable(); err == nil {
		executableDir := filepath.Dir(executable)
		if found := tryDevopsHome(filepath.Join(executableDir, "devops")); !util.IsEmptyString(found) {
			return found
		}
		if found := tryDevopsHome(
			filepath.Join(executableDir, "..", "devops"),
		); !util.IsEmptyString(found) {
			return found
		}
	}
	return ""
}

// defaultManifestFileCandidates lists the manifest paths to try, most authoritative first.
func defaultManifestFileCandidates() []string {
	candidates := make([]string, 0)
	forEachManifestCandidate(func(candidate string) bool {
		candidates = append(candidates, candidate)
		return false
	})
	return candidates
}

func ybaConfFileCandidates() []string {
	if confFile := os.Getenv(ybaConfFileEnvVar); !util.IsEmptyString(confFile) {
		return append([]string{strings.TrimSpace(confFile)}, ybaConfFilePaths...)
	}
	return ybaConfFilePaths
}

// devopsHomeFromYbaConf reads yb.devops.home out of a YBA application.conf. HOCON substitutions
// such as "${?DEVOPS_HOME}" are skipped since the environment is consulted separately.
func devopsHomeFromYbaConf(confPath string) string {
	file, err := os.Open(confPath)
	if err != nil {
		return ""
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if strings.HasPrefix(line, "#") || strings.HasPrefix(line, "//") {
			continue
		}
		match := devopsHomeConfPattern.FindStringSubmatch(line)
		if match == nil {
			continue
		}
		// match[1] is the quoted form, match[2] the bare one; exactly one can be non-empty.
		value := strings.TrimSpace(match[1] + match[2])
		if util.IsEmptyString(value) || strings.HasPrefix(value, "$") {
			continue
		}
		logrus.Debugf("Read yb.devops.home %s from %s", value, confPath)
		return value
	}
	return ""
}
