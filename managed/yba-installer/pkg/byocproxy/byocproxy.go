/*
 * Copyright (c) YugabyteDB, Inc.
 */

// Package byocproxy handles release version resolution, package downloads and pruning
// of old releases for the byoc-api-proxy service. Unlike other yba-installer services,
// byoc-api-proxy has a release lifecycle independent of YugabyteDB Anywhere: packages
// are published to a downloads site (byocApiProxy.downloadBaseUrl) instead of being shipped
// inside the yba_installer_full bundle.
package byocproxy

import (
	"encoding/xml"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

const (
	// LatestVersion is the byocApiProxy.version sentinel that tracks the newest
	// published release rather than pinning a specific one.
	LatestVersion = "latest"

	// maxListBodySize caps a single ListObjectsV2 response page.
	maxListBodySize = 1 << 20 // 1 MiB

	// maxListPages bounds ListObjectsV2 pagination; at up to 1000 folders per
	// page this is far beyond any real release history.
	maxListPages = 16

	// downloadTimeout bounds the whole package download: a stalled connection
	// must fail (and be skipped best effort) rather than hang yba-ctl.
	downloadTimeout = 5 * time.Minute
)

var semverRegex = regexp.MustCompile(`^([0-9]+)\.([0-9]+)\.([0-9]+)$`)

// IsValidVersion reports whether v is an X.Y.Z semver as used by byoc-api-proxy releases.
func IsValidVersion(v string) bool {
	return semverRegex.MatchString(v)
}

// IsLatest reports whether the configured version selects the latest release train.
func IsLatest(configured string) bool {
	c := strings.TrimSpace(configured)
	return c == "" || strings.EqualFold(c, LatestVersion)
}

func parseVersion(v string) ([3]int, error) {
	var parsed [3]int
	matches := semverRegex.FindStringSubmatch(v)
	if matches == nil {
		return parsed, fmt.Errorf("invalid byoc-api-proxy version %q: expected semver X.Y.Z", v)
	}
	for i := 0; i < 3; i++ {
		val, err := strconv.Atoi(matches[i+1])
		if err != nil {
			return parsed, fmt.Errorf("invalid byoc-api-proxy version %q: %w", v, err)
		}
		parsed[i] = val
	}
	return parsed, nil
}

// LessVersions returns true if semver version1 < version2. Both must be valid X.Y.Z.
func LessVersions(version1, version2 string) (bool, error) {
	v1, err := parseVersion(version1)
	if err != nil {
		return false, err
	}
	v2, err := parseVersion(version2)
	if err != nil {
		return false, err
	}
	for i := 0; i < 3; i++ {
		if v1[i] != v2[i] {
			return v1[i] < v2[i], nil
		}
	}
	return false, nil
}

// MaxVersion returns the largest semver in versions, ignoring invalid entries.
// Returns "" when no valid version is present.
func MaxVersion(versions []string) string {
	max := ""
	for _, v := range versions {
		v = strings.TrimSpace(v)
		if !IsValidVersion(v) {
			continue
		}
		if max == "" {
			max = v
			continue
		}
		if less, err := LessVersions(max, v); err == nil && less {
			max = v
		}
	}
	return max
}

// ResolveVersion turns the configured byocApiProxy.version value into a concrete
// version: a pinned X.Y.Z is validated and returned as-is, while "latest" is
// resolved against the versions published at baseURL.
func ResolveVersion(baseURL, configured string) (string, error) {
	if IsLatest(configured) {
		return LatestRemoteVersion(baseURL)
	}
	pinned := strings.TrimSpace(configured)
	if !IsValidVersion(pinned) {
		return "", fmt.Errorf("invalid byocApiProxy.version %q: expected %q or semver X.Y.Z",
			configured, LatestVersion)
	}
	return pinned, nil
}

// LatestRemoteVersion returns the newest version published at baseURL,
// discovered by listing the <version>/ folders through the S3 ListObjectsV2
// API.
func LatestRemoteVersion(baseURL string) (string, error) {
	versions, err := listRemoteVersions(baseURL)
	if err != nil {
		return "", err
	}
	latest := MaxVersion(versions)
	if latest == "" {
		return "", fmt.Errorf("no byoc-api-proxy versions published at %s", baseURL)
	}
	return latest, nil
}

// listBucketResult is the subset of the S3 ListObjectsV2 response consumed for
// version discovery.
type listBucketResult struct {
	XMLName               xml.Name `xml:"ListBucketResult"`
	IsTruncated           bool     `xml:"IsTruncated"`
	NextContinuationToken string   `xml:"NextContinuationToken"`
	CommonPrefixes        []struct {
		Prefix string `xml:"Prefix"`
	} `xml:"CommonPrefixes"`
}

// listRemoteVersions lists the <version>/ folders under baseURL with the S3
// ListObjectsV2 API: GET <baseURL>/?list-type=2&prefix=<path>/&delimiter=/.
// The release site routes this to the bucket root, with the base URL's own
// path as the key prefix, so each published version shows up as one
// CommonPrefixes entry.
func listRemoteVersions(baseURL string) ([]string, error) {
	base, err := url.Parse(strings.TrimRight(baseURL, "/"))
	if err != nil {
		return nil, fmt.Errorf("invalid byoc-api-proxy download URL %q: %w", baseURL, err)
	}
	prefix := strings.Trim(base.Path, "/")
	if prefix != "" {
		prefix += "/"
	}

	client := &http.Client{Timeout: 30 * time.Second}
	var versions []string
	continuationToken := ""
	for page := 0; page < maxListPages; page++ {
		query := url.Values{"list-type": {"2"}, "delimiter": {"/"}}
		if prefix != "" {
			query.Set("prefix", prefix)
		}
		if continuationToken != "" {
			query.Set("continuation-token", continuationToken)
		}
		result, err := fetchListPage(client, base.String()+"/?"+query.Encode())
		if err != nil {
			return nil, err
		}
		for _, cp := range result.CommonPrefixes {
			version := strings.TrimSuffix(strings.TrimPrefix(cp.Prefix, prefix), "/")
			if IsValidVersion(version) {
				versions = append(versions, version)
			}
		}
		if !result.IsTruncated || result.NextContinuationToken == "" {
			return versions, nil
		}
		continuationToken = result.NextContinuationToken
	}
	return nil, fmt.Errorf("byoc-api-proxy version listing at %s did not complete within %d pages",
		baseURL, maxListPages)
}

func fetchListPage(client *http.Client, listURL string) (*listBucketResult, error) {
	resp, err := client.Get(listURL)
	if err != nil {
		return nil, fmt.Errorf("failed to list byoc-api-proxy versions at %s: %w", listURL, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("failed to list byoc-api-proxy versions at %s: %s", listURL, resp.Status)
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, maxListBodySize))
	if err != nil {
		return nil, fmt.Errorf("failed to read byoc-api-proxy version listing at %s: %w", listURL, err)
	}
	var result listBucketResult
	if err := xml.Unmarshal(body, &result); err != nil {
		return nil, fmt.Errorf("%s did not answer with an S3 ListObjectsV2 result - is the "+
			"download site configured to serve bucket listings at this URL? %w", listURL, err)
	}
	return &result, nil
}

// PackageName returns the release tarball name for a version.
func PackageName(version string) string {
	return fmt.Sprintf("byoc_api_proxy-%s.tar.gz", version)
}

// PackageURL returns the release tarball URL for a version, following the
// <base>/<version>/byoc_api_proxy-<version>.tar.gz layout.
func PackageURL(baseURL, version string) string {
	return fmt.Sprintf("%s/%s/%s", strings.TrimRight(baseURL, "/"), version, PackageName(version))
}

// DownloadPackage downloads the release tarball for version into destDir and
// returns the downloaded file path. The download goes to a temporary file that
// is renamed on success, so a partial download never masquerades as a package.
func DownloadPackage(baseURL, version, destDir string) (string, error) {
	url := PackageURL(baseURL, version)
	destPath := filepath.Join(destDir, PackageName(version))

	client := &http.Client{Timeout: downloadTimeout}
	resp, err := client.Get(url)
	if err != nil {
		return "", fmt.Errorf("failed to download byoc-api-proxy package %s: %w", url, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("failed to download byoc-api-proxy package %s: %s", url, resp.Status)
	}

	tmpFile, err := os.CreateTemp(destDir, PackageName(version)+".download-*")
	if err != nil {
		return "", fmt.Errorf("failed to create download file in %s: %w", destDir, err)
	}
	tmpPath := tmpFile.Name()
	defer os.Remove(tmpPath)

	if _, err := io.Copy(tmpFile, resp.Body); err != nil {
		tmpFile.Close()
		return "", fmt.Errorf("failed to download byoc-api-proxy package %s: %w", url, err)
	}
	if err := tmpFile.Close(); err != nil {
		return "", fmt.Errorf("failed to write byoc-api-proxy package to %s: %w", tmpPath, err)
	}
	if err := os.Rename(tmpPath, destPath); err != nil {
		return "", fmt.Errorf("failed to move downloaded package to %s: %w", destPath, err)
	}
	return destPath, nil
}
