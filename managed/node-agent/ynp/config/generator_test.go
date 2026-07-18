// Copyright (c) YugabyteDB, Inc.

package config

import (
	"context"
	"node-agent/model"
	"node-agent/util"
	"os"
	"path/filepath"
	"testing"
)

// Mock data provider to avoid calling YBA APIs.
type MockDataProvider struct {
	sessionInfo  *model.SessionInfo
	provider     *model.Provider
	instanceType *model.NodeInstanceType
	nodeInstance *model.NodeInstance
}

func (dp *MockDataProvider) Load(ctx context.Context) error {
	sessionInfo := &model.SessionInfo{
		CustomerId: "test-customer-id",
	}
	dp.sessionInfo = sessionInfo

	provider := &model.Provider{
		BasicInfo: model.BasicInfo{
			Name: "test-provider-name",
		},
		Details: model.ProviderDetails{
			CloudInfo: model.CloudInfo{
				Onprem: model.OnPremCloudInfo{
					YbHomeDir: "/home/yugabyte",
				},
			},
		},
		Regions: []model.Region{
			{
				BasicInfo: model.BasicInfo{
					Uuid: "test-region-uuid",
					Name: "test-region-name",
				},
				Zones: []model.Zone{
					{
						BasicInfo: model.BasicInfo{
							Uuid: "test-zone-uuid",
							Name: "test-zone-name",
							Code: "test-zone-code",
						},
					},
				},
			},
		},
	}
	provider.Uuid = util.NewUUID().String()
	dp.provider = provider

	instanceType := &model.NodeInstanceType{
		NumCores:  4,
		MemSizeGB: 50,
		Details: model.NodeInstanceTypeDetails{
			VolumeDetailsList: []model.VolumeDetails{
				{
					VolumeSize: 100,
					MountPath:  "/mnt/data1,/mnt/data2",
				},
			},
		},
	}
	dp.instanceType = instanceType

	nodeInstance := &model.NodeInstance{
		ZoneUuid: "test-zone-uuid",
	}
	dp.nodeInstance = nodeInstance
	return nil
}

func (dp *MockDataProvider) GetSessionInfo(ctx context.Context) (*model.SessionInfo, error) {
	return dp.sessionInfo, nil
}

func (dp *MockDataProvider) GetNodeInstance(ctx context.Context) (*model.NodeInstance, error) {
	return dp.nodeInstance, nil
}

func (dp *MockDataProvider) GetProvider(ctx context.Context) (*model.Provider, error) {
	return dp.provider, nil
}

func (dp *MockDataProvider) GetInstanceType(ctx context.Context) (*model.NodeInstanceType, error) {
	return dp.instanceType, nil
}

func (dp *MockDataProvider) GetTmpDirectory(ctx context.Context) (string, error) {
	return "/tmp", nil
}

func (dp *MockDataProvider) GetNodeAgentPort(ctx context.Context) (string, error) {
	return "9070", nil
}

func TestGenerateConfig(t *testing.T) {
	ynpBasePath := filepath.Join(os.Getenv("PROJECT_DIR"), "resources/ynp")
	args := &Args{
		YnpBasePath: ynpBasePath,
		YnpConfig: map[string]map[string]any{
			"yba": {
				"url":                "https://localhost",
				"api_key":            "test-api-key",
				"skip_tls_verify":    true,
				"node_external_fqdn": "test-node",
			},
		},
	}
	dataProvider := &MockDataProvider{}
	gen := NewYNPConfigGenerator(context.Background(), args, dataProvider)
	err := gen.registerResolvers()
	if err != nil {
		t.Fatalf("Failed to register resolvers: %v", err)
	}
	out, err := gen.GenerateYnpConfig()
	if err != nil {
		t.Fatalf("Failed to validate resolvers: %v", err)
	}
	t.Logf("Output: %s\n", out)
	t.Log("All resolvers validated successfully")
}
