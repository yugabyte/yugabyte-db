/*
 * Copyright (c) YugabyteDB, Inc.
 */

package providerutil

import (
	"fmt"
	"slices"

	ybaclient "github.com/yugabyte/platform-go-client"
)

// ValidateImageBundleEdits rejects an --edit-image-bundle or
// --edit-image-bundle-region-override UUID that is not one of bundles, or that
// --remove-image-bundle removes in the same command. The edit helpers act only on a
// matching bundle, so --edit-image-bundle, and AWS's region override, would drop
// such an edit silently. OCI's edit reports an unmatched region override itself, but
// only as an unknown bundle. Pass the bundles from before the removal.
func ValidateImageBundleEdits(
	removeUUIDs, editImageBundles, editRegionOverrides []string,
	bundles []ybaclient.ImageBundle,
) error {
	edits := []struct {
		flag  string
		uuids []string
	}{
		{"--edit-image-bundle", nil},
		{"--edit-image-bundle-region-override", nil},
	}
	for _, raw := range editImageBundles {
		edits[0].uuids = append(edits[0].uuids, BuildImageBundleMapFromString(raw, "edit")["uuid"])
	}
	for _, raw := range editRegionOverrides {
		edits[1].uuids = append(edits[1].uuids,
			BuildImageBundleRegionOverrideMapFromString(raw, "edit")["uuid"])
	}
	for _, edit := range edits {
		for _, uuid := range edit.uuids {
			if !hasImageBundle(bundles, uuid) {
				return notAnImageBundleError(uuid, edit.flag)
			}
			if slices.Contains(removeUUIDs, uuid) {
				return fmt.Errorf(
					"image bundle %s is removed with --remove-image-bundle in the same command "+
						"and cannot also be edited", uuid)
			}
		}
	}
	return nil
}

// RemoveImageBundles returns a copy of bundles without the ones in removeUUIDs.
// It fails on a UUID that is not one of bundles, so a mistyped UUID is not a
// silent no-op.
func RemoveImageBundles(
	removeUUIDs []string,
	bundles []ybaclient.ImageBundle,
) ([]ybaclient.ImageBundle, error) {
	for _, uuid := range removeUUIDs {
		if !hasImageBundle(bundles, uuid) {
			return nil, notAnImageBundleError(uuid, "--remove-image-bundle")
		}
	}
	return slices.DeleteFunc(slices.Clone(bundles), func(ib ybaclient.ImageBundle) bool {
		return slices.Contains(removeUUIDs, ib.GetUuid())
	}), nil
}

func hasImageBundle(bundles []ybaclient.ImageBundle, uuid string) bool {
	return slices.ContainsFunc(bundles, func(ib ybaclient.ImageBundle) bool {
		return ib.GetUuid() == uuid
	})
}

func notAnImageBundleError(uuid, flag string) error {
	return fmt.Errorf("image bundle %s in %s is not an image bundle of the provider", uuid, flag)
}
