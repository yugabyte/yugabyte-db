/*
 * Copyright (c) YugabyteDB, Inc.
 */

package providerutil

import (
	"fmt"
	"slices"

	ybaclient "github.com/yugabyte/platform-go-client"
)

// RemoveImageBundles returns a copy of bundles without the ones in removeUUIDs.
// It fails on a UUID that is not one of bundles, so a mistyped UUID is not a
// silent no-op.
func RemoveImageBundles(
	removeUUIDs []string,
	bundles []ybaclient.ImageBundle,
) ([]ybaclient.ImageBundle, error) {
	for _, uuid := range removeUUIDs {
		if !slices.ContainsFunc(bundles, func(ib ybaclient.ImageBundle) bool {
			return ib.GetUuid() == uuid
		}) {
			return nil, fmt.Errorf(
				"image bundle %s in --remove-image-bundle is not an image bundle of the provider",
				uuid)
		}
	}
	return slices.DeleteFunc(slices.Clone(bundles), func(ib ybaclient.ImageBundle) bool {
		return slices.Contains(removeUUIDs, ib.GetUuid())
	}), nil
}
