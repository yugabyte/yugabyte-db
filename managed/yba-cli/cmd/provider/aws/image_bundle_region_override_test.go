/*
 * Copyright (c) YugabyteDB, Inc.
 */

package aws

import (
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestEditAWSImageBundleRegionOverrides(t *testing.T) {
	bundles := func() []ybaclient.ImageBundle {
		return []ybaclient.ImageBundle{
			awsBundle("u1", map[string]ybaclient.BundleInfo{
				"us-west-2": {
					YbImage:         util.GetStringPointer("ami-old"),
					SshUserOverride: util.GetStringPointer("centos"),
				},
				"us-east-1": {YbImage: util.GetStringPointer("ami-east")},
			}),
			awsBundle("u2", map[string]ybaclient.BundleInfo{
				"us-west-2": {YbImage: util.GetStringPointer("ami-other")},
			}),
		}
	}

	t.Run("updates the named region of the named bundle", func(t *testing.T) {
		got, err := editAWSImageBundles(nil, []string{
			"image-bundle-uuid=u1::region-name=us-west-2::machine-image=ami-new",
		}, bundles())
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		regions := bundleRegions(got[0])
		if west := regions["us-west-2"]; west.GetYbImage() != "ami-new" ||
			west.GetSshUserOverride() != "centos" {
			t.Errorf("u1 us-west-2 = (%q, ssh-user %q), want (ami-new, centos)",
				west.GetYbImage(), west.GetSshUserOverride())
		}
		if east := regions["us-east-1"]; east.GetYbImage() != "ami-east" {
			t.Errorf("u1 us-east-1 = %q, want it unchanged", east.GetYbImage())
		}
		if other := bundleRegions(got[1])["us-west-2"]; other.GetYbImage() != "ami-other" {
			t.Errorf("u2 us-west-2 = %q, want it unchanged", other.GetYbImage())
		}
	})

	t.Run("omitted machine-image leaves the region unchanged", func(t *testing.T) {
		got, err := editAWSImageBundles(nil, []string{
			"image-bundle-uuid=u1::region-name=us-west-2",
		}, bundles())
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		if west := bundleRegions(got[0])["us-west-2"]; west.GetYbImage() != "ami-old" {
			t.Errorf("u1 us-west-2 = %q, want ami-old", west.GetYbImage())
		}
	})

	t.Run("unknown region fails", func(t *testing.T) {
		_, err := editAWSImageBundles(nil, []string{
			"image-bundle-uuid=u1::region-name=eu-west-1::machine-image=ami-new",
		}, bundles())
		want := "region eu-west-1 in --edit-image-bundle-region-override is not a region of image bundle u1"
		if err == nil || err.Error() != want {
			t.Fatalf("got error %v, want %q", err, want)
		}
	})
}

func awsBundle(uuid string, regions map[string]ybaclient.BundleInfo) ybaclient.ImageBundle {
	return ybaclient.ImageBundle{
		Uuid:    util.GetStringPointer(uuid),
		Details: &ybaclient.ImageBundleDetails{Regions: &regions},
	}
}

func bundleRegions(bundle ybaclient.ImageBundle) map[string]ybaclient.BundleInfo {
	details := bundle.GetDetails()
	return details.GetRegions()
}
