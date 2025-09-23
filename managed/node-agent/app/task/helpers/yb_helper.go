// Copyright (c) YugabyteDB, Inc.

package helpers

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
)

// Release stores the YB release info.
type Release struct {
	Version string
	Name    string
}

// OSInfo represents parsed OS release info
type OSInfo struct {
	ID      string // e.g., "ubuntu"
	Family  string // e.g., "debian"
	Pretty  string // e.g., "Ubuntu 22.04.4 LTS"
	Arch    string // e.g., "x86_64"
	Version string // e.g., "22"
}

var releaseFormat = regexp.MustCompile(`yugabyte[-_]([\d]+\.[\d]+\.[\d]+\.[\d]+-[a-z0-9]+)`)

// extractReleaseFromArchive parses the archive filename and returns a Release.
func extractReleaseFromArchive(filename string) (*Release, error) {
	matches := releaseFormat.FindStringSubmatch(filename)
	if len(matches) < 2 {
		return nil, fmt.Errorf("invalid archive filename: %s", filename)
	}
	version := matches[1]
	return &Release{
		Version: version,
		Name:    "yugabyte-" + version,
	}, nil
}

// ExtractArchiveFolderName returns the folder name without ".tar.gz" suffix.
func ExtractArchiveFolderName(filename string) string {
	return strings.TrimSuffix(filename, ".tar.gz")
}

// ExtractYugabyteReleaseFolder returns the release directory path from archive filename.
func ExtractYugabyteReleaseFolder(filename string) (string, error) {
	release, err := extractReleaseFromArchive(filename)
	if err != nil {
		return "", err
	}
	return filepath.Join("releases", release.Name), nil
}

// ExtractReleaseVersion returns the version extracted from archive filename.
func ExtractReleaseVersion(filename string) (string, error) {
	release, err := extractReleaseFromArchive(filename)
	if err != nil {
		return "", err
	}
	return release.Version, nil
}

// ListDirectoryContent returns the list of entries (files/directories) in a given directory.
func ListDirectoryContent(dirPath string) ([]string, error) {
	entries, err := os.ReadDir(dirPath)
	if err != nil {
		return nil, fmt.Errorf("Failed to list directory %s: %w", dirPath, err)
	}
	names := make([]string, len(entries))
	for i, entry := range entries {
		names[i] = entry.Name()
	}
	return names, nil
}

// GetOSInfo parses /etc/os-release and returns OS info
func GetOSInfo() (*OSInfo, error) {
	file, err := os.Open("/etc/os-release")
	if err != nil {
		return nil, fmt.Errorf("failed to open /etc/os-release: %w", err)
	}
	defer file.Close()

	info := &OSInfo{}
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		// Remove quotes from values
		if keyVal := strings.SplitN(line, "=", 2); len(keyVal) == 2 {
			key := keyVal[0]
			val := strings.Trim(keyVal[1], `"`)
			switch key {
			case "ID":
				info.ID = strings.ToLower(val)
			case "ID_LIKE":
				info.Family = strings.ToLower(val)
			case "PRETTY_NAME":
				info.Pretty = val
			case "VERSION_ID":
				if parts := strings.SplitN(val, ".", 2); len(parts) > 0 {
					info.Version = parts[0]
				}
			}
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("error reading /etc/os-release: %w", err)
	}
	info.Arch = runtime.GOARCH
	return info, nil
}

func IsRhel9(osInfo *OSInfo) bool {
	return (strings.Contains(osInfo.Family, "rhel") || strings.Contains(osInfo.ID, "rhel")) &&
		osInfo.Version == "9"
}
