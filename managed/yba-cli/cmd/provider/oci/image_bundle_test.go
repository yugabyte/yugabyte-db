/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"maps"
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestOCIRegionImages(t *testing.T) {
	one := []string{"us-ashburn-1"}
	two := []string{"us-ashburn-1", "us-phoenix-1"}
	tests := []struct {
		name              string
		regionCodes       []string
		machineImage      string
		overrides         map[string]string
		requireAllRegions bool
		want              map[string]string
		wantErr           string
	}{
		{
			name:         "single region machine-image",
			regionCodes:  one,
			machineImage: "img-a",
			want:         map[string]string{"us-ashburn-1": "img-a"},
		},
		{
			name:         "multi region machine-image",
			regionCodes:  two,
			machineImage: "img-a",
			wantErr:      "provider has 2 regions",
		},
		{
			name:        "no regions",
			overrides:   map[string]string{"us-ashburn-1": "img-a"},
			wantErr:     "at least one region",
			regionCodes: nil,
		},
		{
			name:         "machine-image with overrides",
			regionCodes:  one,
			machineImage: "img-a",
			overrides:    map[string]string{"us-ashburn-1": "img-b"},
			wantErr:      "cannot be combined",
		},
		{
			name:        "single region without image",
			regionCodes: one,
			wantErr:     "machine-image not specified",
		},
		{
			name:              "add with override per region",
			regionCodes:       two,
			overrides:         map[string]string{"us-ashburn-1": "img-a", "us-phoenix-1": "img-b"},
			requireAllRegions: true,
			want:              map[string]string{"us-ashburn-1": "img-a", "us-phoenix-1": "img-b"},
		},
		{
			name:              "add missing a region",
			regionCodes:       two,
			overrides:         map[string]string{"us-ashburn-1": "img-a"},
			requireAllRegions: true,
			wantErr:           "region us-phoenix-1 is missing an image OCID",
		},
		{
			name:        "edit one of several regions",
			regionCodes: two,
			overrides:   map[string]string{"us-phoenix-1": "img-b"},
			want:        map[string]string{"us-phoenix-1": "img-b"},
		},
		{
			name:        "override for unknown region",
			regionCodes: one,
			overrides:   map[string]string{"us-ashburn-1": "img-a", "eu-frankfurt-1": "img-b"},
			wantErr:     "region eu-frankfurt-1, which is not configured",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ociRegionImages(
				tc.regionCodes, tc.machineImage, tc.overrides, tc.requireAllRegions)
			checkErr(t, err, tc.wantErr)
			if tc.wantErr == "" && !maps.Equal(got, tc.want) {
				t.Errorf("got %v, want %v", got, tc.want)
			}
		})
	}
}

func TestOCIOverridesByBundle(t *testing.T) {
	got, err := ociOverridesByBundle([]string{
		"image-bundle-name=b1::region-name=us-ashburn-1::machine-image=img-a",
		"image-bundle-name=b1::region-name=us-phoenix-1::machine-image=img-b",
		"image-bundle-name=b2::region-name=us-ashburn-1::machine-image=img-c",
	}, "add")
	checkErr(t, err, "")
	if len(got) != 2 || got["b1"]["us-phoenix-1"] != "img-b" || got["b2"]["us-ashburn-1"] != "img-c" {
		t.Errorf("unexpected grouping %v", got)
	}

	_, err = ociOverridesByBundle([]string{
		"image-bundle-uuid=u1::region-name=us-ashburn-1::machine-image=img-a",
		"image-bundle-uuid=u1::region-name=us-ashburn-1::machine-image=img-b",
	}, "edit")
	checkErr(t, err, "more than one image-bundle region override for region us-ashburn-1")
}

func TestUnmatchedOverridesError(t *testing.T) {
	checkErr(t, unmatchedOverridesError(nil), "")
	err := unmatchedOverridesError(map[string]map[string]string{
		"zeta": {}, "alpha": {},
	})
	checkErr(t, err, "unknown image bundles: alpha, zeta")
}

func TestAddOCIImageBundles(t *testing.T) {
	one := []string{"us-ashburn-1"}

	got, err := addOCIImageBundles([]string{
		"image-bundle-name=b1::machine-image=img-a::ssh-user=opc",
	}, nil, one, nil)
	checkErr(t, err, "")
	if len(got) != 1 {
		t.Fatalf("got %d bundles, want 1", len(got))
	}
	details := got[0].GetDetails()
	info := details.GetRegions()["us-ashburn-1"]
	if info.GetYbImage() != "img-a" || details.GlobalYbImage != nil {
		t.Errorf("image not set on details.regions: %+v", details)
	}
	if !got[0].GetUseAsDefault() {
		t.Errorf("only bundle for the arch should be default")
	}

	_, err = addOCIImageBundles([]string{
		"image-bundle-name=b1::machine-image=img-a::ssh-user=opc",
		"image-bundle-name=b1::machine-image=img-b::ssh-user=opc",
	}, nil, one, nil)
	checkErr(t, err, "image bundle name b1 is given more than once")

	_, err = addOCIImageBundles([]string{
		"image-bundle-name=b1::machine-image=img-a::ssh-user=opc",
	}, nil, one, []string{
		"image-bundle-name=typo::region-name=us-ashburn-1::machine-image=img-b",
	})
	checkErr(t, err, "unknown image bundles: typo")

	got, err = addOCIImageBundles([]string{
		"image-bundle-name=b1::machine-image=img-a::ssh-user=opc",
		"image-bundle-name=b2::machine-image=img-b::ssh-user=opc::default=true",
	}, nil, one, nil)
	checkErr(t, err, "")
	if got[0].GetUseAsDefault() || !got[1].GetUseAsDefault() {
		t.Errorf("explicit default must not be shadowed by auto-promotion")
	}
}

func TestEditOCIImageBundlesKeepsEarlierImage(t *testing.T) {
	regions := map[string]ybaclient.BundleInfo{
		"us-ashburn-1": {YbImage: util.GetStringPointer("old")},
	}
	existing := []ybaclient.ImageBundle{{
		Uuid:    util.GetStringPointer("u1"),
		Details: &ybaclient.ImageBundleDetails{Regions: &regions, SshUser: util.GetStringPointer("opc")},
	}}
	got, err := editOCIImageBundles([]string{
		"image-bundle-uuid=u1::machine-image=img-a",
		"image-bundle-uuid=u1::ssh-user=yb",
	}, existing, []string{"us-ashburn-1"}, nil)
	checkErr(t, err, "")
	details := got[0].GetDetails()
	info := details.GetRegions()["us-ashburn-1"]
	if info.GetYbImage() != "img-a" || details.GetSshUser() != "yb" {
		t.Errorf("got image %q ssh-user %q", info.GetYbImage(), details.GetSshUser())
	}
}

func TestApplyOCIRegionImageEdit(t *testing.T) {
	details := ybaclient.ImageBundleDetails{}
	details.SetRegions(map[string]ybaclient.BundleInfo{
		"us-ashburn-1": {YbImage: util.GetStringPointer("old-a")},
		"us-phoenix-1": {YbImage: util.GetStringPointer("old-b")},
	})
	err := applyOCIRegionImageEdit(&details, []string{"us-ashburn-1", "us-phoenix-1"}, "",
		map[string]string{"us-phoenix-1": "new-b"})
	checkErr(t, err, "")
	regions := details.GetRegions()
	a, b := regions["us-ashburn-1"], regions["us-phoenix-1"]
	if a.GetYbImage() != "old-a" || b.GetYbImage() != "new-b" {
		t.Errorf("unexpected regions after edit: %v", regions)
	}
}

func TestValidateOCIBundlesCoverAddedRegions(t *testing.T) {
	bundle := func(name, bundleType string, regions ...string) ybaclient.ImageBundle {
		info := map[string]ybaclient.BundleInfo{}
		for _, r := range regions {
			info[r] = ybaclient.BundleInfo{YbImage: util.GetStringPointer("img-" + r)}
		}
		return ybaclient.ImageBundle{
			Name:     util.GetStringPointer(name),
			Uuid:     util.GetStringPointer(name + "-uuid"),
			Details:  &ybaclient.ImageBundleDetails{Regions: &info},
			Metadata: &ybaclient.Metadata{Type: bundleType},
		}
	}

	err := validateOCIBundlesCoverAddedRegions([]string{"b"}, []ybaclient.ImageBundle{
		bundle("yba", "YBA_ACTIVE", "a"),
		bundle("custom", customImageBundleType, "a", "b"),
	})
	checkErr(t, err, "")

	err = validateOCIBundlesCoverAddedRegions([]string{"b"}, []ybaclient.ImageBundle{
		bundle("custom", customImageBundleType, "a"),
	})
	checkErr(t, err,
		"image-bundle-uuid=custom-uuid::region-name=b::machine-image=<image-ocid>")
}

func TestValidateOCIImageBundlesNotRemoved(t *testing.T) {
	checkErr(t, validateOCIImageBundlesNotRemoved(
		[]string{"u1"},
		[]string{"image-bundle-uuid=u2::ssh-user=opc"},
		[]string{"image-bundle-uuid=u3::region-name=r::machine-image=img"},
	), "")
	checkErr(t, validateOCIImageBundlesNotRemoved(
		[]string{"u1"},
		nil,
		[]string{"image-bundle-uuid=u1::region-name=r::machine-image=img"},
	), "image bundle u1 is removed")
	checkErr(t, validateOCIImageBundlesNotRemoved(
		[]string{"u1"},
		[]string{"image-bundle-uuid=u1::ssh-user=opc"},
		nil,
	), "image bundle u1 is removed")
}

func checkErr(t *testing.T, err error, want string) {
	t.Helper()
	if want == "" {
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		return
	}
	if err == nil || !strings.Contains(err.Error(), want) {
		t.Fatalf("got error %v, want one containing %q", err, want)
	}
}
