package testutils

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"testing"
)

func GetVersion(tb testing.TB) string {
	tb.Helper()
	// First, check if there is an environment variable set for the version
	if version := os.Getenv("YBA_INSTALLER_VERSION"); version != "" {
		return version
	}

	// If not, get the version from version_metadata.json, and then find the latest build.
	version, err := getLatestVersionFromMetadata()
	if err != nil {
		tb.Fatalf("failed to get latest version from metadata: %v", err)
	}
	return version
}

func getLatestVersionFromMetadata() (string, error) {
	versionNumber, err := getVersionNumberFromMetadata()
	if err != nil {
		return "", fmt.Errorf("failed to get version number from metadata: %w", err)
	}
	return getLatestVersionFromS3(versionNumber)
}

func GetTopDir() string {
	// Get the absolute path of the current directory
	absPath, err := os.Getwd()
	if err != nil {
		panic(fmt.Sprintf("failed to get current working directory: %v", err))
	}
	pathParts := strings.Split(absPath, string(os.PathSeparator))
	// Find the index of "yba-installer"
	for i, part := range pathParts {
		if part == "yba-installer" {
			// Return the path up to and including "yba-installer"
			return "/" + filepath.Join(pathParts[:i+1]...)
		}
	}
	// If "yba-installer" is not found, return the current directory as a fallback
	return absPath
}

func getVersionNumberFromMetadata() (string, error) {
	f, err := os.Open(filepath.Join(GetTopDir(), "version_metadata.json"))
	if err != nil {
		return "", err
	}
	defer f.Close()
	var metadata struct {
		Version string `json:"version_number"`
	}
	if err := json.NewDecoder(f).Decode(&metadata); err != nil {
		return "", err
	}
	if metadata.Version == "" {
		return "", fmt.Errorf("version field is empty in version_metadata.json")
	}
	return metadata.Version, nil
}

func getLatestVersionFromS3(versionNumber string) (string, error) {
	client := NewS3Client()
	versions, err := client.List("releases.yugabyte.com", versionNumber)
	if err != nil {
		return "", fmt.Errorf("failed to list versions in S3: %w", err)
	}
	slices.SortFunc(versions, CompareVersions)
	for i := len(versions) - 1; i >= 0; i-- {
		// Skip test builds which have a version number like 2.27.0.0-b99947123
		if strings.Contains(versions[i], "999") {
			continue
		}
		versionFiles, err := client.List("releases.yugabyte.com", versions[i])
		if err != nil {
			return "", fmt.Errorf("failed to list version files in S3: %w", err)
		}
		for _, file := range versionFiles {
			if strings.Contains(file, "yba_installer_full") && strings.Contains(file, "centos-x86_64.tar.gz") {
				// Found the latest version file
				return strings.TrimRight(versions[i], "/"), nil
			}
		}
	}
	return "", fmt.Errorf("no valid version found in S3 for version number %s", versionNumber)
}

// Version sorting here
type versionStruct struct {
	major  int
	minor  int
	patch  int
	hotfix int
	build  int
}

func parseVersion(version string) versionStruct {
	regex, err := regexp.Compile(`^([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)-b([0-9]+)/?$`)
	if err != nil {
		panic(err)
	}
	matches := regex.FindAllStringSubmatch(version, -1)
	if matches == nil || len(matches) != 1 || len(matches[0]) != 6 {
		panic("invalid version '" + version + "'")
	}
	return versionStruct{
		major:  atoi(matches[0][1]),
		minor:  atoi(matches[0][2]),
		patch:  atoi(matches[0][3]),
		hotfix: atoi(matches[0][4]),
		build:  atoi(matches[0][5]),
	}
}
func atoi(s string) int {
	i, err := strconv.Atoi(s)
	if err != nil {
		panic(err)
	}
	return i
}

func CompareVersions(a, b string) int {
	av := parseVersion(a)
	bv := parseVersion(b)

	if av.major != bv.major {
		return av.major - bv.major
	}
	if av.minor != bv.minor {
		return av.minor - bv.minor
	}
	if av.patch != bv.patch {
		return av.patch - bv.patch
	}
	if av.hotfix != bv.hotfix {
		return av.hotfix - bv.hotfix
	}
	return av.build - bv.build
}
