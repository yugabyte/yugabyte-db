/*
 * Copyright (c) YugabyteDB, Inc.
 */

package providerutil

import (
	"slices"
	"strings"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestRemoveImageBundles(t *testing.T) {
	const (
		uuidA   = "11111111-1111-1111-1111-111111111111"
		uuidB   = "22222222-2222-2222-2222-222222222222"
		uuidC   = "33333333-3333-3333-3333-333333333333"
		unknown = "00000000-0000-0000-0000-000000000000"
	)
	bundles := func() []ybaclient.ImageBundle {
		var out []ybaclient.ImageBundle
		for _, u := range []string{uuidA, uuidB, uuidC} {
			out = append(out, ybaclient.ImageBundle{Uuid: util.GetStringPointer(u)})
		}
		return out
	}
	tests := []struct {
		name    string
		remove  []string
		want    []string
		wantErr string
	}{
		{name: "nothing to remove", want: []string{uuidA, uuidB, uuidC}},
		{name: "remove first", remove: []string{uuidA}, want: []string{uuidB, uuidC}},
		{name: "remove middle", remove: []string{uuidB}, want: []string{uuidA, uuidC}},
		{name: "remove last", remove: []string{uuidC}, want: []string{uuidA, uuidB}},
		{name: "remove two", remove: []string{uuidC, uuidA}, want: []string{uuidB}},
		{name: "unknown uuid", remove: []string{unknown}, wantErr: unknown},
		{name: "unknown with known uuid", remove: []string{uuidA, unknown}, wantErr: unknown},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			in := bundles()
			got, err := RemoveImageBundles(tc.remove, in)
			for i, ib := range in {
				if ib.GetUuid() != []string{uuidA, uuidB, uuidC}[i] {
					t.Fatalf("input slice modified: %v", in)
				}
			}
			if tc.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("err = %v, want error naming %s", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			var gotUUIDs []string
			for _, ib := range got {
				gotUUIDs = append(gotUUIDs, ib.GetUuid())
			}
			if !slices.Equal(gotUUIDs, tc.want) {
				t.Fatalf("remaining = %v, want %v", gotUUIDs, tc.want)
			}
		})
	}
}

func TestValidateImageBundleEdits(t *testing.T) {
	bundles := []ybaclient.ImageBundle{
		{Uuid: util.GetStringPointer("u1")},
		{Uuid: util.GetStringPointer("u2")},
	}
	tests := []struct {
		name            string
		remove          []string
		edit            []string
		editOverride    []string
		wantErrContains string
	}{
		{name: "no edits"},
		{
			name:         "edits of known bundles",
			remove:       []string{"u1"},
			edit:         []string{"image-bundle-uuid=u2::ssh-user=opc"},
			editOverride: []string{"image-bundle-uuid=u2::region-name=r::machine-image=img"},
		},
		{
			name:            "unknown uuid in edit-image-bundle",
			edit:            []string{"image-bundle-uuid=typo::ssh-user=opc"},
			wantErrContains: "image bundle typo in --edit-image-bundle is not an image bundle of the provider",
		},
		{
			name:         "unknown uuid in edit-image-bundle-region-override",
			editOverride: []string{"image-bundle-uuid=typo::region-name=r::machine-image=img"},
			wantErrContains: "image bundle typo in --edit-image-bundle-region-override " +
				"is not an image bundle of the provider",
		},
		{
			name:            "edit-image-bundle of a removed bundle",
			remove:          []string{"u1"},
			edit:            []string{"image-bundle-uuid=u1::ssh-user=opc"},
			wantErrContains: "image bundle u1 is removed with --remove-image-bundle",
		},
		{
			name:            "edit-image-bundle-region-override of a removed bundle",
			remove:          []string{"u1"},
			editOverride:    []string{"image-bundle-uuid=u1::region-name=r::machine-image=img"},
			wantErrContains: "image bundle u1 is removed with --remove-image-bundle",
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ValidateImageBundleEdits(tc.remove, tc.edit, tc.editOverride, bundles)
			if tc.wantErrContains == "" {
				if err != nil {
					t.Fatalf("unexpected error: %v", err)
				}
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.wantErrContains) {
				t.Fatalf("got error %v, want one containing %q", err, tc.wantErrContains)
			}
		})
	}
}
