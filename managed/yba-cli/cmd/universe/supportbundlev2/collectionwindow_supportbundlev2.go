/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

const defaultCollectionWindow = 24 * time.Hour

func formatCollectionTime(t time.Time) string {
	return t.UTC().Format("2006-01-02T15:04:05Z")
}

// isUnsetValue reports whether a yaml/flag value is absent, treating the "<placeholder>" form used
// throughout the shipped manifest as unset.
func isUnsetValue(raw string) bool {
	value := strings.TrimSpace(raw)
	if util.IsEmptyString(value) {
		return true
	}
	if strings.HasPrefix(value, "<") && strings.HasSuffix(value, ">") {
		return true
	}
	return false
}
func defaultCollectionWindowRange() (string, string) {
	endTime := time.Now().UTC()
	startTime := endTime.Add(-defaultCollectionWindow)
	return formatCollectionTime(startTime), formatCollectionTime(endTime)
}
func parseCollectionTime(raw string) (time.Time, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return time.Time{}, fmt.Errorf("empty time value")
	}
	// Strict RFC3339, as v1 parses --start-time/--end-time. The zoneless forms accepted before
	// were read as UTC, silently shifting the window for anyone who meant local time.
	parsed, err := time.Parse(time.RFC3339, raw)
	if err != nil {
		return time.Time{}, fmt.Errorf("invalid time %q, want RFC3339 (2006-01-02T15:04:05Z)", raw)
	}
	return parsed.UTC(), nil
}

// resolveCollectionTimeRange picks the window from --start-time / --end-time alone: both given are
// used as given, end only starts defaultCollectionWindow before it, start only ends at now, neither
// is the last defaultCollectionWindow. Never taken from the manifest, whose dates are as likely to
// be weeks stale as current; warnOnManifestWindow reports when it carries some.
func resolveCollectionTimeRange(cmd *cobra.Command) (string, string, error) {
	startFlag, _ := cmd.Flags().GetString("start-time")
	endFlag, _ := cmd.Flags().GetString("end-time")
	startFlag = strings.TrimSpace(startFlag)
	endFlag = strings.TrimSpace(endFlag)
	endGiven := !isUnsetValue(endFlag)

	now := time.Now().UTC()

	endTime := now
	if endGiven {
		parsed, err := parseCollectionTime(endFlag)
		if err != nil {
			return "", "", fmt.Errorf("invalid --end-time: %w", err)
		}
		// parseCollectionTime yields whole seconds and now carries sub-second precision, so an
		// --end-time scripted from `date -u` compares as past rather than tripping this.
		if parsed.After(now) {
			return "", "", fmt.Errorf(
				"--end-time %s is in the future, it must be at or before now (%s)",
				formatCollectionTime(parsed), formatCollectionTime(now))
		}
		endTime = parsed
	}

	startTime := endTime.Add(-defaultCollectionWindow)
	if !isUnsetValue(startFlag) {
		parsed, err := parseCollectionTime(startFlag)
		if err != nil {
			return "", "", fmt.Errorf("invalid --start-time: %w", err)
		}
		startTime = parsed
	}

	if !startTime.Before(endTime) {
		// With no --end-time the end is now, so naming a flag the operator did not pass would
		// only confuse; say what the bound actually is.
		if !endGiven {
			return "", "", fmt.Errorf(
				"--start-time %s must be in the past, before %s",
				formatCollectionTime(startTime), formatCollectionTime(endTime))
		}
		return "", "", fmt.Errorf(
			"--start-time %s must be before --end-time %s",
			formatCollectionTime(startTime), formatCollectionTime(endTime))
	}

	return formatCollectionTime(startTime), formatCollectionTime(endTime), nil
}

// warnOnManifestWindow reports a manifest that carries collectionTimeRange dates. The shipped
// manifest no longer declares the key, so this only fires on a hand-written one - which is exactly
// where a window left over from an earlier collection would otherwise go unnoticed.
func warnOnManifestWindow(cfg *supportBundleV2Config) {
	if cfg == nil {
		return
	}
	start := strings.TrimSpace(cfg.Spec.CollectionTimeRange.StartDate)
	end := strings.TrimSpace(cfg.Spec.CollectionTimeRange.EndDate)
	if isUnsetValue(start) && isUnsetValue(end) {
		return
	}
	logrus.Warnf(
		"Ignoring collectionTimeRange in the manifest (startDate: %q, endDate: %q). "+
			"The collection window comes from --start-time / --end-time, "+
			"defaulting to the last %.0fh.\n",
		start, end, defaultCollectionWindow.Hours())
}

// resolveNodeNames picks the node selection using, in order:
// 1) the --node-names CLI flag
// 2) spec.nodeSelection.nodeNames in the yaml config
// 3) nil, meaning every node in the universe
func resolveNodeNames(cmd *cobra.Command, cfg *supportBundleV2Config) []string {
	flagValue, _ := cmd.Flags().GetString("node-names")
	if names := normalizeNodeNames(strings.Split(flagValue, ",")); len(names) > 0 {
		return names
	}
	return normalizeNodeNames(cfg.Spec.NodeSelection.NodeNames)
}

// normalizeNodeNames trims, drops blanks and yaml placeholders, and de-duplicates while keeping
// the caller's ordering.
func normalizeNodeNames(raw []string) []string {
	var names []string
	seen := make(map[string]struct{}, len(raw))
	for _, name := range raw {
		name = strings.TrimSpace(name)
		if isUnsetValue(name) {
			continue
		}
		if _, duplicate := seen[name]; duplicate {
			continue
		}
		seen[name] = struct{}{}
		names = append(names, name)
	}
	return names
}

// collectionTimeForSpec parses a bound produced by formatCollectionTime back into the spec's
// timestamp. Re-parsing the formatted text rather than reusing the original time keeps the spec
// field and the scripts' --start_date / --end_date derived from one string, and drops the
// sub-second precision that would otherwise serialize as RFC3339Nano.
func collectionTimeForSpec(raw string) (*time.Time, error) {
	if util.IsEmptyString(raw) {
		return nil, nil
	}
	parsed, err := parseCollectionTime(raw)
	if err != nil {
		return nil, err
	}
	truncated := parsed.UTC().Truncate(time.Second)
	return &truncated, nil
}
