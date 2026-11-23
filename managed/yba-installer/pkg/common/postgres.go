// Copyright (c) YugabyteDB, Inc.

package common

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// PostgresVersion is the version of the bundled postgres, set during build.
var PostgresVersion = ""

func PostgresMajorVersion(version string) int {
	major, _ := strconv.Atoi(strings.Split(version, ".")[0])
	return major
}

// InstalledPostgresDataDir returns the data directory set in the active install's postgresql.conf.
func InstalledPostgresDataDir() (string, error) {
	confPath := filepath.Join(GetActiveSymlink(), "pgsql/conf/postgresql.conf")
	contents, err := os.ReadFile(confPath)
	if err != nil {
		return "", fmt.Errorf("failed to read %s: %w", confPath, err)
	}
	// The last setting wins, as it does for postgres.
	dataDir := ""
	for _, line := range strings.Split(string(contents), "\n") {
		if value, found := strings.CutPrefix(line, "data_directory ="); found {
			dataDir = strings.Trim(strings.TrimSpace(value), "'")
		}
	}
	if dataDir == "" {
		return "", fmt.Errorf("data_directory is not set in %s", confPath)
	}
	return dataDir, nil
}

// PostgresClusterMajorVersion returns the major version from the PG_VERSION file of the cluster in
// dataDir.
func PostgresClusterMajorVersion(dataDir string) (int, error) {
	contents, err := os.ReadFile(filepath.Join(dataDir, "PG_VERSION"))
	if err != nil {
		return 0, fmt.Errorf("failed to read the postgres version of %s: %w", dataDir, err)
	}
	return strconv.Atoi(strings.TrimSpace(string(contents)))
}
