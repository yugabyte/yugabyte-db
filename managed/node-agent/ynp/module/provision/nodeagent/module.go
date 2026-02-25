// Copyright (c) YugabyteDB, Inc.

package nodeagent

import (
	"bytes"
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"node-agent/model"
	"node-agent/util"
	"node-agent/ynp/config"
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

func (m *InstallNodeAgent) getHeaders(token string) map[string]string {
	return map[string]string{
		"Accept":              "application/json",
		"X-AUTH-YW-API-TOKEN": token,
		"Content-Type":        "application/json",
	}
}

func (m *InstallNodeAgent) getProviderURL(values map[string]any) string {
	url := values["url"].(string)
	customerUUID := values["customer_uuid"].(string)
	providerName := values["provider_name"].(string)
	return fmt.Sprintf("%s/api/v1/customers/%s/providers?name=%s", url, customerUUID, providerName)
}

func (m *InstallNodeAgent) getInstanceTypeURL(
	ybaURL, customerUUID, providerUUID, code string,
) string {
	return fmt.Sprintf(
		"%s/api/v1/customers/%s/providers/%s/instance_types/%s",
		ybaURL,
		customerUUID,
		providerUUID,
		code,
	)
}

func (m *InstallNodeAgent) makeRequest(
	url, method string,
	headers map[string]string,
	data interface{},
	verifySSL bool,
) ([]byte, int, error) {
	var reqBody []byte
	var err error
	if data != nil {
		reqBody, err = json.Marshal(data)
		if err != nil {
			return nil, 0, err
		}
	}
	client := &http.Client{}
	if !verifySSL {
		tr := &http.Transport{
			TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
		}
		client.Transport = tr
	}
	var req *http.Request
	if reqBody != nil {
		req, err = http.NewRequest(method, url, bytes.NewBuffer(reqBody))
	} else {
		req, err = http.NewRequest(method, url, nil)
	}
	if err != nil {
		return nil, 0, err
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, 0, err
	}
	defer resp.Body.Close()
	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, resp.StatusCode, err
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return respBody, resp.StatusCode, fmt.Errorf("HTTP error: %d", resp.StatusCode)
	}
	return respBody, resp.StatusCode, nil
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
			CloudInfo: model.CloudInfo{
				Onprem: model.OnPremCloudInfo{
					YbHomeDir:     ybHomeDir,
					UseClockbound: useClockbound,
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

func (m *InstallNodeAgent) getProvider(values map[string]any) (*model.Provider, error) {
	providerURL := m.getProviderURL(values)
	ybaURL := values["url"].(string)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaURL), "https") ||
		config.GetBool(values, "skip_tls_verify", false)
	headers := m.getHeaders(values["api_key"].(string))
	resp, _, err := m.makeRequest(providerURL, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var providers []model.Provider
	if err := json.Unmarshal(resp, &providers); err != nil {
		return nil, err
	}
	if len(providers) == 0 {
		return nil, util.ErrNotExist
	}
	return &providers[0], err
}

func (m *InstallNodeAgent) createInstanceIfNotExists(
	ctx context.Context,
	values map[string]any,
	provider *model.Provider,
) error {
	ybaURL := values["url"].(string)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaURL), "https") ||
		config.GetBool(values, "skip_tls_verify", false)
	getInstanceTypeURL := m.getInstanceTypeURL(
		values["url"].(string),
		values["customer_uuid"].(string),
		provider.Uuid,
		values["instance_type_name"].(string),
	)
	headers := m.getHeaders(values["api_key"].(string))
	resp, status, err := m.makeRequest(getInstanceTypeURL, "GET", headers, nil, skipTLSVerify)
	if err == nil && status == 200 {
		var data interface{}
		if err := json.Unmarshal(resp, &data); err == nil && data != nil {
			return nil // instance type exists
		}
	}
	if status == 400 || (err != nil && strings.Contains(err.Error(), "400")) {
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
func (m *InstallNodeAgent) listProviderNodeInstancesURL(values map[string]any) string {
	url := values["url"].(string)
	customerUUID := values["customer_uuid"].(string)
	providerUUID := values["provider_id"].(string)
	return fmt.Sprintf(
		"%s/api/v1/customers/%s/providers/%s/nodes/list",
		url,
		customerUUID,
		providerUUID,
	)
}

func (m *InstallNodeAgent) getProviderNodeInstances(
	values map[string]any,
) ([]model.NodeInstance, error) {
	providerNodesUrl := m.listProviderNodeInstancesURL(values)
	ybaURL := values["url"].(string)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaURL), "https") ||
		config.GetBool(values, "skip_tls_verify", false)
	headers := m.getHeaders(values["api_key"].(string))
	resp, _, err := m.makeRequest(providerNodesUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var instances []model.NodeInstance
	if err := json.Unmarshal(resp, &instances); err != nil {
		return nil, err
	}
	return instances, nil
}

func (m *InstallNodeAgent) checkIfNodeInstanceAlreadyExists(
	ctx context.Context,
	values map[string]any,
	input *model.NodeDetails,
) (bool, error) {
	instances, err := m.getProviderNodeInstances(values)
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
	nodeAgentEnabled := false
	provider, err := m.getProvider(values)
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
		if !regionExists || !zoneExists {
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
		nodeAgentEnabled = provider.Details.EnableNodeAgent
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
		nodeAgentEnabled = true
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
	if nodeAgentEnabled {
		return m.BaseModule.RenderTemplates(ctx, values)
	}
	return nil, nil
}
