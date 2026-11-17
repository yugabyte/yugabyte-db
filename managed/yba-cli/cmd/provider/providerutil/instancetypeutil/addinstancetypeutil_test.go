/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

import (
	"reflect"
	"testing"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestBuildInstanceTypeRequest(t *testing.T) {
	const providerUUID = "11111111-2222-3333-4444-555555555555"
	const shape = "VM.Test.Flex"
	defaults := addInstanceTypeFlags{memSize: 8, numCores: 4}
	existingVolumes := []ybaclient.VolumeDetails{
		{MountPath: "/mnt/d0", VolumeSizeGB: 250, VolumeType: "SSD"},
	}
	existing := func(arch *string) *ybaclient.InstanceTypeResp {
		return &ybaclient.InstanceTypeResp{
			IdKey: ybaclient.InstanceTypeKey{
				InstanceTypeCode: shape,
				ProviderUuid:     providerUUID,
			},
			NumCores:  util.GetFloat64Pointer(2),
			MemSizeGB: util.GetFloat64Pointer(16),
			InstanceTypeDetails: &ybaclient.InstanceTypeDetails{
				Tenancy:           util.GetStringPointer("Dedicated"),
				Arch:              arch,
				VolumeDetailsList: existingVolumes,
			},
		}
	}
	request := func(
		cores, mem float64,
		tenancy, arch *string,
		volumes []ybaclient.VolumeDetails,
	) ybaclient.InstanceType {
		return ybaclient.InstanceType{
			IdKey: ybaclient.InstanceTypeKey{
				InstanceTypeCode: shape,
				ProviderUuid:     providerUUID,
			},
			NumCores:  util.GetFloat64Pointer(cores),
			MemSizeGB: util.GetFloat64Pointer(mem),
			InstanceTypeDetails: &ybaclient.InstanceTypeDetails{
				Tenancy:           tenancy,
				Arch:              arch,
				VolumeDetailsList: volumes,
			},
		}
	}
	dedicated := util.GetStringPointer("Dedicated")
	newVolumes := []ybaclient.VolumeDetails{
		{MountPath: "/mnt/d1", VolumeSizeGB: 100, VolumeType: "SSD"},
	}

	tests := []struct {
		name         string
		providerType string
		existing     *ybaclient.InstanceTypeResp
		flags        addInstanceTypeFlags
		changed      []string
		want         ybaclient.InstanceType
	}{
		{
			name:         "existing type without flags keeps its values",
			providerType: util.OCIProviderType,
			existing:     existing(nil),
			flags:        defaults,
			want:         request(2, 16, dedicated, nil, existingVolumes),
		},
		{
			name:         "existing type with num-cores changes only cores",
			providerType: util.OCIProviderType,
			existing:     existing(nil),
			flags:        addInstanceTypeFlags{memSize: 8, numCores: 6},
			changed:      []string{"num-cores"},
			want:         request(6, 16, dedicated, nil, existingVolumes),
		},
		{
			name:         "existing type with volume replaces volumes",
			providerType: util.OCIProviderType,
			existing:     existing(nil),
			flags: addInstanceTypeFlags{
				memSize: 8, numCores: 4, volumes: []string{"mount-points=/mnt/d1"},
			},
			changed: []string{"volume"},
			want:    request(2, 16, dedicated, nil, newVolumes),
		},
		{
			name:         "existing type with every flag set uses the flags",
			providerType: util.OCIProviderType,
			existing:     existing(nil),
			flags: addInstanceTypeFlags{
				memSize: 32, numCores: 8, tenancy: "Shared",
				volumes: []string{"mount-points=/mnt/d1"},
			},
			changed: []string{"mem-size", "num-cores", "tenancy", "volume"},
			want:    request(8, 32, util.GetStringPointer("Shared"), nil, newVolumes),
		},
		{
			name:         "new type uses flag defaults",
			providerType: util.OCIProviderType,
			flags:        defaults,
			want: request(4, 8, util.GetStringPointer(""), nil,
				[]ybaclient.VolumeDetails{}),
		},
		{
			name:         "existing onprem type without volume keeps volumes",
			providerType: util.OnpremProviderType,
			existing:     existing(nil),
			flags:        defaults,
			want:         request(2, 16, dedicated, nil, existingVolumes),
		},
		{
			name:         "existing aws type keeps arch",
			providerType: util.AWSProviderType,
			existing:     existing(util.GetStringPointer("aarch64")),
			flags:        addInstanceTypeFlags{memSize: 8, numCores: 4, arch: "x86_64"},
			want: request(2, 16, dedicated, util.GetStringPointer("aarch64"),
				existingVolumes),
		},
		{
			name:         "existing aws type with arch flag changes arch",
			providerType: util.AWSProviderType,
			existing:     existing(util.GetStringPointer("aarch64")),
			flags:        addInstanceTypeFlags{memSize: 8, numCores: 4, arch: "x86_64"},
			changed:      []string{"arch"},
			want: request(2, 16, dedicated, util.GetStringPointer("x86_64"),
				existingVolumes),
		},
		{
			name:         "existing aws type without arch uses arch flag default",
			providerType: util.AWSProviderType,
			existing:     existing(nil),
			flags:        addInstanceTypeFlags{memSize: 8, numCores: 4, arch: "x86_64"},
			want: request(2, 16, dedicated, util.GetStringPointer("x86_64"),
				existingVolumes),
		},
		{
			name:         "existing type without details",
			providerType: util.GCPProviderType,
			existing: &ybaclient.InstanceTypeResp{
				NumCores:  util.GetFloat64Pointer(2),
				MemSizeGB: util.GetFloat64Pointer(16),
			},
			flags: defaults,
			want:  request(2, 16, nil, nil, nil),
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			changed := func(flag string) bool {
				for _, c := range tc.changed {
					if c == flag {
						return true
					}
				}
				return false
			}
			got := buildInstanceTypeRequest(
				providerUUID, tc.providerType, shape, tc.existing, tc.flags, changed)
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("got %+v (details %+v), want %+v (details %+v)",
					got, got.InstanceTypeDetails, tc.want, tc.want.InstanceTypeDetails)
			}
		})
	}
}
