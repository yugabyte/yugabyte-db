// Copyright (c) YugabyteDB, Inc.

package nodeagent

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"node-agent/model"
	"node-agent/util"
	"node-agent/ynp/config"
	"node-agent/ynp/yba"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const ModuleName = "InstallNodeAgent"

type InstallNodeAgent struct {
	*config.BaseModule
}

func NewInstallNodeAgent(basePath string) config.Module {
	return &InstallNodeAgent{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "node_agent")),
	}
}

func (m *InstallNodeAgent) generateProviderPayload(
	values map[string]any,
) (*model.Provider, error) {
	timestamp := time.Now().Unix()
	providerName := values["provider_name"].(string)
	ybHomeDir := "/home/yugabyte"
	if v, ok := values["yb_home_dir"].(string); ok {
		ybHomeDir = v
	}
	useClockbound := config.GetBool(values, "configure_clockbound", false)
	enableMultiTenancy := config.GetBool(values, "configure_cgroup", true)
	regionName := values["provider_region_name"].(string)
	zoneName := values["provider_region_zone_name"].(string)
	region := model.Region{
		BasicInfo: model.BasicInfo{
			Name: regionName,
			Code: regionName,
		},
		Zones: []model.Zone{
			{
				BasicInfo: model.BasicInfo{
					Name: zoneName,
					Code: zoneName,
				},
			},
		},
	}
	if lat := config.GetFloat(values, "provider_region_latitude", 0.0); lat >= -90 && lat <= 90 {
		region.Latitude = lat
	}
	if lon := config.GetFloat(values, "provider_region_longitude", 0.0); lon >= -180 && lon <= 180 {
		region.Longitude = lon
	}
	nodeExporterPort := config.GetInt(values, "node_exporter_port", 9300)
	var chronyServers []string
	if str, ok := values["chrony_servers"].(string); ok {
		for _, token := range strings.Split(str, ",") {
			token = strings.TrimSpace(token)
			if token != "" {
				chronyServers = append(chronyServers, token)
			}
		}
	}
	provider := model.Provider{
		BasicInfo: model.BasicInfo{
			Name: providerName,
			Code: "onprem",
		},
		Cuuid:         values["customer_uuid"].(string),
		AirGapInstall: config.GetBool(values, "is_airgap", false),
		Details: model.ProviderDetails{
			SkipProvisioning:    true, /* Manual provisioning */
			InstallNodeExporter: true, /* Default is true */
			NodeExporterPort:    int(nodeExporterPort),
			CloudInfo: model.CloudInfo{
				Onprem: model.OnPremCloudInfo{
					YbHomeDir:          ybHomeDir,
					UseClockbound:      useClockbound,
					EnableMultiTenancy: enableMultiTenancy,
				},
			},
		},
		Regions: []model.Region{region},
	}
	if len(chronyServers) > 0 {
		// Populate NTP servers if provided.
		provider.Details.NtpServers = chronyServers
	}
	if keyPath, ok := values["provider_access_key_path"].(string); ok && keyPath != "" {
		keyContent, err := os.ReadFile(keyPath)
		if err != nil {
			return nil, err
		}
		provider.AllAccessKeys = []model.AccessKey{
			{
				KeyInfo: model.KeyInfo{
					KeyPairName:              fmt.Sprintf("onprem_key_%d.pem", timestamp),
					SshPrivateKeyContent:     strings.TrimSpace(string(keyContent)),
					SkipKeyValidateAndUpload: false,
				},
			},
		}
	}
	return &provider, nil
}

func (m *InstallNodeAgent) generateProviderUpdatePayload(
	values map[string]any,
	provider *model.Provider,
) *model.Provider {
	// Patch enableMultiTenancy onto existing on-prem cloudInfo so reprovision propagates the
	// toggled configure_cgroup value to YBA.
	provider.Details.CloudInfo.Onprem.EnableMultiTenancy = config.GetBool(
		values, "configure_cgroup", true)

	regionExist := false
	regionName := values["provider_region_name"].(string)
	zoneName := values["provider_region_zone_name"].(string)
	for i := range provider.Regions {
		// Get the pointer to the region to modify its zones.
		region := &provider.Regions[i]
		if region.Code == regionName {
			regionExist = true
			zoneExist := false
			for _, zone := range region.Zones {
				if zone.Code == zoneName {
					zoneExist = true
					break
				}
			}
			if !zoneExist {
				region.Zones = append(region.Zones, model.Zone{
					BasicInfo: model.BasicInfo{
						Name: zoneName,
						Code: zoneName,
					},
				})
			}
		}
	}
	if !regionExist {
		provider.Regions = append(provider.Regions, model.Region{
			BasicInfo: model.BasicInfo{
				Name: regionName,
				Code: regionName,
			},
			Zones: []model.Zone{
				{
					BasicInfo: model.BasicInfo{
						Name: zoneName,
						Code: zoneName,
					},
				},
			},
		})
	}
	return provider
}

func (m *InstallNodeAgent) generateInstanceTypePayload(
	values map[string]any,
) (*model.NodeInstanceType, error) {
	instanceTypeName := values["instance_type_name"].(string)
	numCores := config.GetFloat(values, "instance_type_cores", 0.0)
	memSizeGB := config.GetFloat(values, "instance_type_memory_size", 0.0)
	volumeSizeGB := config.GetFloat(values, "instance_type_volume_size", 0.0)
	mountPoints, ok := values["instance_type_mount_points"].(string)
	if !ok || mountPoints == "" {
		return nil, errors.New(
			"instance_type_mount_points is required but not provided in configuration",
		)
	}
	mountPoints = strings.Trim(mountPoints, "[]")
	mountPoints = strings.ReplaceAll(mountPoints, "'", "")
	mps := strings.Split(mountPoints, ", ")
	volumeDetails := []model.VolumeDetails{}
	for _, mp := range mps {
		volumeDetails = append(volumeDetails, model.VolumeDetails{
			VolumeType: "SSD",
			VolumeSize: volumeSizeGB,
			MountPath:  mp,
		})
	}
	return &model.NodeInstanceType{
		IDKey: model.InstanceTypeKey{
			InstanceTypeCode: instanceTypeName,
		},
		Active:       true,
		ProviderUuid: "$provider_id",
		NumCores:     numCores,
		MemSizeGB:    memSizeGB,
		Details: model.NodeInstanceTypeDetails{
			VolumeDetailsList: volumeDetails,
		},
	}, nil
}

func (m *InstallNodeAgent) generateAddNodePayload(values map[string]any) *model.NodeInstances {
	return &model.NodeInstances{
		Nodes: []model.NodeDetails{
			{
				IP:           values["node_external_fqdn"].(string),
				Region:       values["provider_region_name"].(string),
				Zone:         values["provider_region_zone_name"].(string),
				InstanceType: values["instance_type_name"].(string),
				InstanceName: values["node_name"].(string),
			},
		},
	}
}

func (m *InstallNodeAgent) getProvider(
	ctx context.Context,
	values map[string]any,
) (*model.Provider, error) {
	ybaURL := values["url"].(string)
	apiKey := values["api_key"].(string)
	skipTlsVerify := config.GetBool(values, "skip_tls_verify", true)
	customerUUID := values["customer_uuid"].(string)
	providerName := values["provider_name"].(string)
	return yba.GetProviderByName(ctx, ybaURL, apiKey, skipTlsVerify, customerUUID, providerName)
}

func (m *InstallNodeAgent) createInstanceIfNotExists(
	ctx context.Context,
	values map[string]any,
	provider *model.Provider,
) error {
	ybaUrl := values["url"].(string)
	apiKey := values["api_key"].(string)
	skipTlsVerify := config.GetBool(values, "skip_tls_verify", true)
	customerUuid := values["customer_uuid"].(string)
	instanceTypeName := values["instance_type_name"].(string)
	_, err := yba.GetInstanceType(ctx,
		ybaUrl,
		apiKey,
		skipTlsVerify,
		customerUuid,
		provider.Uuid,
		instanceTypeName,
	)
	if err == nil {
		// Instance type already exists, no need to create.
		return nil
	}
	if err == util.ErrNotExist {
		util.ConsoleLogger().Info(ctx, "Instance type does not exist, creating it.")
		instanceData, err := m.generateInstanceTypePayload(values)
		if err != nil {
			return err
		}
		instancePayloadFile := filepath.Join(
			values["tmp_directory"].(string),
			"create_instance.json",
		)
		f, ferr := os.Create(instancePayloadFile)
		if ferr != nil {
			return ferr
		}
		defer f.Close()
		enc := json.NewEncoder(f)
		enc.SetIndent("", "    ")
		return enc.Encode(instanceData)
	}
	return err
}

func (m *InstallNodeAgent) getProviderNodeInstances(
	ctx context.Context,
	values map[string]any,
) ([]model.NodeInstance, error) {
	ybaURL := values["url"].(string)
	apiKey := values["api_key"].(string)
	customerUUID := values["customer_uuid"].(string)
	providerUUID := values["provider_id"].(string)
	skipTlsVerify := config.GetBool(values, "skip_tls_verify", true)
	return yba.GetProviderNodeInstances(
		ctx,
		ybaURL,
		apiKey,
		skipTlsVerify,
		customerUUID,
		providerUUID,
	)
}

func (m *InstallNodeAgent) checkIfNodeInstanceAlreadyExists(
	ctx context.Context,
	values map[string]any,
	input *model.NodeDetails,
) (bool, error) {
	instances, err := m.getProviderNodeInstances(ctx, values)
	if err != nil {
		return false, err
	}
	for _, instance := range instances {
		if instance.Details.IP == input.IP {
			if instance.Details.Region != input.Region || instance.Details.Zone != input.Zone {
				util.FileLogger().
					Errorf(ctx, "Node with IP %s already exists in different region/zone: %s/%s",
						input.IP,
						instance.Details.Region,
						instance.Details.Zone,
					)
				return false, fmt.Errorf(
					"Node with IP %s already exists in different region/zone: %s/%s",
					input.IP,
					instance.Details.Region,
					instance.Details.Zone,
				)
			}
			if instance.Details.InstanceType != input.InstanceType {
				util.FileLogger().
					Errorf(ctx, "Node with IP %s already exists with different instance type: %s",
						input.IP,
						instance.Details.InstanceType,
					)
				return false, fmt.Errorf(
					"Node with IP %s already exists with different instance type: %s",
					input.IP,
					instance.Details.InstanceType,
				)
			}
			if instance.InstanceName != input.InstanceName {
				util.FileLogger().
					Errorf(ctx, "Node with IP %s already exists with different instance name: %s",
						input.IP,
						instance.InstanceName,
					)
				return false, fmt.Errorf(
					"Node with IP %s already exists with different instance name: %s",
					input.IP,
					instance.InstanceName,
				)
			}
			return true, nil
		}
	}
	return false, nil
}

func (m *InstallNodeAgent) cleanup(ctx context.Context, values map[string]any) {
	filesToRemove := []string{
		"create_provider.json",
		"update_provider.json",
		"create_instance.json",
		"add_node_to_provider.json",
	}
	tmpDir, ok := values["tmp_directory"].(string)
	if !ok {
		util.ConsoleLogger().Info(ctx, "Temporary directory not found")
		return
	}
	for _, fileName := range filesToRemove {
		filePath := filepath.Join(tmpDir, fileName)
		if _, err := os.Stat(filePath); err == nil {
			os.Remove(filePath)
		}
	}
}

func (m *InstallNodeAgent) RenderTemplates(
	ctx context.Context,
	values map[string]any,
) (*config.RenderedTemplates, error) {
	if config.GetBool(values, "is_cloud", false) {
		// Not implemented: call base module's RenderTemplates
		return nil, nil
	}
	m.cleanup(ctx, values)
	provider, err := m.getProvider(ctx, values)
	if err != nil && err != util.ErrNotExist {
		return nil, err
	}
	if err == nil {
		providerYbHomeDir := provider.Details.CloudInfo.Onprem.YbHomeDir
		if providerYbHomeDir != "" && providerYbHomeDir != values["yb_home_dir"].(string) {
			return nil, fmt.Errorf(
				"Provider YugabyteDB home directory (%s) does not match the configured value (%s)",
				providerYbHomeDir,
				values["yb_home_dir"].(string),
			)
		}
		regionExists := false
		zoneExists := false
		regionName := values["provider_region_name"].(string)
		zoneName := values["provider_region_zone_name"].(string)
		for _, region := range provider.Regions {
			if region.Code == regionName {
				regionExists = true
				for _, zone := range region.Zones {
					if zone.Code == zoneName {
						zoneExists = true
						break
					}
				}
				break
			}
		}
		// Also write the update payload when configure_cgroup diverges from what YBA has,
		// so toggling it on reprovision actually reaches the provider.
		multiTenancyDiff := provider.Details.CloudInfo.Onprem.EnableMultiTenancy !=
			config.GetBool(values, "configure_cgroup", true)
		if !regionExists || !zoneExists || multiTenancyDiff {
			updateProviderData := m.generateProviderUpdatePayload(values, provider)
			updateProviderDataFile := filepath.Join(
				values["tmp_directory"].(string),
				"update_provider.json",
			)
			f, ferr := os.Create(updateProviderDataFile)
			if ferr != nil {
				return nil, ferr
			}
			defer f.Close()
			enc := json.NewEncoder(f)
			enc.SetIndent("", "    ")
			if err := enc.Encode(updateProviderData); err != nil {
				return nil, err
			}
		}
		if err := m.createInstanceIfNotExists(ctx, values, provider); err != nil {
			return nil, err
		}
		values["provider_id"] = provider.Uuid
	} else {
		util.ConsoleLogger().Info(ctx, "Generating provider create payload...")
		providerPayload, err := m.generateProviderPayload(values)
		if err != nil {
			return nil, err
		}
		providerPayloadFile := filepath.Join(values["tmp_directory"].(string), "create_provider.json")
		f, ferr := os.Create(providerPayloadFile)
		if ferr != nil {
			return nil, ferr
		}
		defer f.Close()
		enc := json.NewEncoder(f)
		enc.SetIndent("", "    ")
		if err := enc.Encode(providerPayload); err != nil {
			return nil, err
		}
		instanceCreatePayload, err := m.generateInstanceTypePayload(values)
		if err != nil {
			return nil, err
		}
		instancePayloadFile := filepath.Join(values["tmp_directory"].(string), "create_instance.json")
		f2, ferr2 := os.Create(instancePayloadFile)
		if ferr2 != nil {
			return nil, ferr2
		}
		defer f2.Close()
		enc2 := json.NewEncoder(f2)
		enc2.SetIndent("", "    ")
		if err := enc2.Encode(instanceCreatePayload); err != nil {
			return nil, err
		}
	}
	nodeAlreadyExists := false
	addNodePayload := m.generateAddNodePayload(values)
	if _, ok := values["provider_id"].(string); ok {
		exists, err := m.checkIfNodeInstanceAlreadyExists(ctx, values, &addNodePayload.Nodes[0])
		if err != nil {
			return nil, err
		}
		nodeAlreadyExists = exists
	}
	if !nodeAlreadyExists {
		addNodePayloadFile := filepath.Join(
			values["tmp_directory"].(string),
			"add_node_to_provider.json",
		)
		f3, ferr3 := os.Create(addNodePayloadFile)
		if ferr3 != nil {
			return nil, ferr3
		}
		defer f3.Close()
		enc3 := json.NewEncoder(f3)
		enc3.SetIndent("", "    ")
		if err := enc3.Encode(addNodePayload); err != nil {
			return nil, err
		}
	}
	return m.BaseModule.RenderTemplates(ctx, values)
}
