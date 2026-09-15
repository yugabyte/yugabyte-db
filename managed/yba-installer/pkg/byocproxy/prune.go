/*
 * Copyright (c) YugabyteDB, Inc.
 */

package byocproxy

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
)

// PruneOldReleases removes old extracted releases under baseDir and their
// cached tarballs under downloadDir, with the same keep policy as
// common.PrunePastInstalls uses for the YBA software dir: every version
// directory except the active version and the newest remaining one (a
// rollback target) is removed. Tarballs are only removed together with their
// version directory, so a manually dropped package for a future upgrade
// (airgapped hosts) is left alone. Temp files from interrupted downloads are
// cleaned up as well. Returns the removed paths; errors are collected so one
// failed removal does not stop the rest.
func PruneOldReleases(baseDir, downloadDir, active string) ([]string, error) {
	if !IsValidVersion(active) {
		return nil, fmt.Errorf("invalid active byoc-api-proxy version %q", active)
	}
	entries, err := os.ReadDir(baseDir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	// The download cache, the 'active' symlink and anything else that is not a
	// version directory is skipped.
	var candidates []string
	for _, entry := range entries {
		if entry.IsDir() && IsValidVersion(entry.Name()) && entry.Name() != active {
			candidates = append(candidates, entry.Name())
		}
	}
	keep := MaxVersion(candidates)

	var removed []string
	var errs []error
	remove := func(path string) {
		if _, err := os.Lstat(path); err != nil {
			return
		}
		if err := os.RemoveAll(path); err != nil {
			errs = append(errs, err)
			return
		}
		removed = append(removed, path)
	}
	for _, version := range candidates {
		if version == keep {
			continue
		}
		remove(filepath.Join(baseDir, version))
		remove(filepath.Join(downloadDir, PackageName(version)))
	}
	// Interrupted downloads leave <package>.download-* temp files behind.
	tmpFiles, _ := filepath.Glob(filepath.Join(downloadDir, "*.download-*"))
	for _, path := range tmpFiles {
		remove(path)
	}
	return removed, errors.Join(errs...)
}
