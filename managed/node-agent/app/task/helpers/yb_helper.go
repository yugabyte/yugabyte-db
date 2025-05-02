// Copyright (c) YugaByte, Inc.

package helpers

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

// Release stores the YB release info.
type Release struct {
	Version string
	Name    string
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
