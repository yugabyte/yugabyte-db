// Copyright (c) YugabyteDB, Inc.

package nodeagent

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"node-agent/model"
	"reflect"
	"strings"
	"testing"
)

func TestGenerateProviderPayload(t *testing.T) {
	m := &InstallNodeAgent{}
	values := map[string]any{
		"provider_name":             "onprem-provider",
		"customer_uuid":             "customer-123",
		"provider_region_name":      "region-a",
		"provider_region_zone_name": "zone-a",
	}

	provider, err := m.generateProviderPayload(values)
	if err != nil {
		t.Fatalf("generateProviderPayload() error = %v", err)
	}

	want := &model.Provider{
		BasicInfo: model.BasicInfo{
			Name: "onprem-provider",
			Code: "onprem",
		},
		Cuuid: "customer-123",
		Details: model.ProviderDetails{
			SkipProvisioning:    true,
			InstallNodeExporter: true,
			NodeExporterPort:    9300,
			CloudInfo: model.CloudInfo{
				Onprem: model.OnPremCloudInfo{
					YbHomeDir:     "/home/yugabyte",
					UseClockbound: false,
				},
			},
		},
		Regions: []model.Region{
			{
				BasicInfo: model.BasicInfo{
					Name: "region-a",
					Code: "region-a",
				},
				Zones: []model.Zone{
					{
						BasicInfo: model.BasicInfo{
							Name: "zone-a",
							Code: "zone-a",
						},
					},
				},
			},
		},
	}
	if !reflect.DeepEqual(provider, want) {
		t.Fatalf("generateProviderPayload() = %#v, want %#v", provider, want)
	}
}

func TestGenerateProviderUpdatePayloadAddZone(t *testing.T) {
	m := &InstallNodeAgent{}
	values := map[string]any{
		"provider_region_name":      "region-a",
		"provider_region_zone_name": "zone-b",
		"configure_cgroup":          false,
	}
	provider := &model.Provider{
		Regions: []model.Region{
			{
				BasicInfo: model.BasicInfo{Name: "region-a", Code: "region-a"},
				Zones: []model.Zone{
					{BasicInfo: model.BasicInfo{Name: "zone-a", Code: "zone-a"}},
				},
			},
		},
	}

	updated := m.generateProviderUpdatePayload(values, provider)
	if len(updated.Regions) != 1 {
		t.Fatalf("Regions length = %d, want 1", len(updated.Regions))
	}
	if len(updated.Regions[0].Zones) != 2 {
		t.Fatalf("Zones length = %d, want 2", len(updated.Regions[0].Zones))
	}
	if updated.Regions[0].Zones[1].Code != "zone-b" {
		t.Fatalf("added zone code = %q, want zone-b", updated.Regions[0].Zones[1].Code)
	}
}

func TestGenerateProviderUpdatePayload_AddRegion(t *testing.T) {
	m := &InstallNodeAgent{}
	values := map[string]any{
		"provider_region_name":      "region-b",
		"provider_region_zone_name": "zone-b",
	}
	provider := &model.Provider{
		Regions: []model.Region{
			{
				BasicInfo: model.BasicInfo{Name: "region-a", Code: "region-a"},
				Zones: []model.Zone{
					{BasicInfo: model.BasicInfo{Name: "zone-a", Code: "zone-a"}},
				},
			},
		},
	}

	updated := m.generateProviderUpdatePayload(values, provider)
	if len(updated.Regions) != 2 {
		t.Fatalf("Regions length = %d, want 2", len(updated.Regions))
	}
	newRegion := updated.Regions[1]
	if newRegion.Code != "region-b" || len(newRegion.Zones) != 1 ||
		newRegion.Zones[0].Code != "zone-b" {
		t.Fatalf("unexpected new region: %#v", newRegion)
	}
}

func TestGenerateInstanceTypePayloadSuccess(t *testing.T) {
	m := &InstallNodeAgent{}
	values := map[string]any{
		"instance_type_name":         "c5.large",
		"instance_type_cores":        8.0,
		"instance_type_memory_size":  32.0,
		"instance_type_volume_size":  250.0,
		"instance_type_mount_points": "['/mnt/d0', '/mnt/d1']",
	}

	instanceType, err := m.generateInstanceTypePayload(values)
	if err != nil {
		t.Fatalf("generateInstanceTypePayload() error = %v", err)
	}

	want := &model.NodeInstanceType{
		IDKey: model.InstanceTypeKey{
			InstanceTypeCode: "c5.large",
		},
		Active:       true,
		ProviderUuid: "$provider_id",
		NumCores:     8.0,
		MemSizeGB:    32.0,
		Details: model.NodeInstanceTypeDetails{
			VolumeDetailsList: []model.VolumeDetails{
				{VolumeType: "SSD", VolumeSize: 250.0, MountPath: "/mnt/d0"},
				{VolumeType: "SSD", VolumeSize: 250.0, MountPath: "/mnt/d1"},
			},
		},
	}
	if !reflect.DeepEqual(instanceType, want) {
		t.Fatalf("generateInstanceTypePayload() = %#v, want %#v", instanceType, want)
	}
}

func TestGenerateInstanceTypePayloadMissingMountPoints(t *testing.T) {
	m := &InstallNodeAgent{}
	values := map[string]any{
		"instance_type_name": "c5.large",
	}

	_, err := m.generateInstanceTypePayload(values)
	if err == nil {
		t.Fatalf("generateInstanceTypePayload() error = nil, want error")
	}
}

func TestGenerateAddNodePayload(t *testing.T) {
	m := &InstallNodeAgent{}
	values := map[string]any{
		"node_external_fqdn":        "node.example.com",
		"provider_region_name":      "region-a",
		"provider_region_zone_name": "zone-a",
		"instance_type_name":        "c5.large",
		"node_name":                 "node-1",
	}

	payload := m.generateAddNodePayload(values)
	want := &model.NodeInstances{
		Nodes: []model.NodeDetails{
			{
				IP:           "node.example.com",
				Region:       "region-a",
				Zone:         "zone-a",
				InstanceType: "c5.large",
				InstanceName: "node-1",
			},
		},
	}
	if !reflect.DeepEqual(payload, want) {
		t.Fatalf("generateAddNodePayload() = %#v, want %#v", payload, want)
	}
}

func TestRenderTemplatesCloudSkips(t *testing.T) {
	m := NewInstallNodeAgent("/opt/ynp").(*InstallNodeAgent)
	values := map[string]any{"is_cloud": true}

	rendered, err := m.RenderTemplates(context.Background(), values)
	if err != nil {
		t.Fatalf("RenderTemplates() error = %v", err)
	}
	if rendered != nil {
		t.Fatalf("RenderTemplates() = %#v, want nil", rendered)
	}
}

func TestCheckIfNodeInstanceAlreadyExists(t *testing.T) {
	tests := []struct {
		name      string
		instances []model.NodeInstance
		input     model.NodeDetails
		wantExist bool
		wantErr   string
	}{
		{
			name:      "no matching ip",
			instances: []model.NodeInstance{},
			input: model.NodeDetails{
				IP:           "10.0.0.1",
				Region:       "region-a",
				Zone:         "zone-a",
				InstanceType: "c5.large",
				InstanceName: "node-1",
			},
			wantExist: false,
		},
		{
			name: "matching node",
			instances: []model.NodeInstance{
				{
					InstanceName: "node-1",
					Details: model.NodeDetails{
						IP:           "10.0.0.1",
						Region:       "region-a",
						Zone:         "zone-a",
						InstanceType: "c5.large",
					},
				},
			},
			input: model.NodeDetails{
				IP:           "10.0.0.1",
				Region:       "region-a",
				Zone:         "zone-a",
				InstanceType: "c5.large",
				InstanceName: "node-1",
			},
			wantExist: true,
		},
		{
			name: "different region",
			instances: []model.NodeInstance{
				{
					InstanceName: "node-1",
					Details: model.NodeDetails{
						IP:           "10.0.0.1",
						Region:       "region-b",
						Zone:         "zone-a",
						InstanceType: "c5.large",
					},
				},
			},
			input: model.NodeDetails{
				IP:           "10.0.0.1",
				Region:       "region-a",
				Zone:         "zone-a",
				InstanceType: "c5.large",
				InstanceName: "node-1",
			},
			wantErr: "already exists in different region/zone",
		},
		{
			name: "different instance type",
			instances: []model.NodeInstance{
				{
					InstanceName: "node-1",
					Details: model.NodeDetails{
						IP:           "10.0.0.1",
						Region:       "region-a",
						Zone:         "zone-a",
						InstanceType: "m5.large",
					},
				},
			},
			input: model.NodeDetails{
				IP:           "10.0.0.1",
				Region:       "region-a",
				Zone:         "zone-a",
				InstanceType: "c5.large",
				InstanceName: "node-1",
			},
			wantErr: "already exists with different instance type",
		},
		{
			name: "different instance name",
			instances: []model.NodeInstance{
				{
					InstanceName: "node-old",
					Details: model.NodeDetails{
						IP:           "10.0.0.1",
						Region:       "region-a",
						Zone:         "zone-a",
						InstanceType: "c5.large",
					},
				},
			},
			input: model.NodeDetails{
				IP:           "10.0.0.1",
				Region:       "region-a",
				Zone:         "zone-a",
				InstanceType: "c5.large",
				InstanceName: "node-1",
			},
			wantErr: "already exists with different instance name",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// This mocks getProviderNodeInstances.
			server := httptest.NewServer(
				http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
					if err := json.NewEncoder(w).Encode(tt.instances); err != nil {
						t.Errorf("failed to encode response: %v", err)
					}
				}),
			)
			defer server.Close()

			m := &InstallNodeAgent{}
			values := map[string]any{
				"url":           server.URL,
				"api_key":       "test-api-key",
				"customer_uuid": "customer-123",
				"provider_id":   "provider-123",
			}

			exists, err := m.checkIfNodeInstanceAlreadyExists(
				context.Background(),
				values,
				&tt.input,
			)
			if tt.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tt.wantErr) {
					t.Fatalf(
						"checkIfNodeInstanceAlreadyExists() error = %v, want error containing %q",
						err,
						tt.wantErr,
					)
				}
				return
			}
			if err != nil {
				t.Fatalf("checkIfNodeInstanceAlreadyExists() error = %v", err)
			}
			if exists != tt.wantExist {
				t.Fatalf("checkIfNodeInstanceAlreadyExists() = %v, want %v", exists, tt.wantExist)
			}
		})
	}
}
