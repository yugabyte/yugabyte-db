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
	"log"
	"net/http"
	"node-agent/util"
	"node-agent/ynp/config"
	"os"
	"path/filepath"
	"strconv"
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
) (map[string]interface{}, error) {
	timestamp := time.Now().Unix()
	providerName := values["provider_name"].(string)
	ybHomeDir := "/home/yugabyte"
	if v, ok := values["yb_home_dir"].(string); ok {
		ybHomeDir = v
	}
	useClockbound := "false"
	if v, ok := values["configure_clockbound"].(string); ok {
		useClockbound = v
	}
	regionName := values["provider_region_name"].(string)
	zoneName := values["provider_region_zone_name"].(string)
	region := map[string]interface{}{
		"name": regionName,
		"code": regionName,
		"zones": []map[string]interface{}{
			{"name": zoneName, "code": zoneName},
		},
	}
	if lat, ok := values["provider_region_latitude"].(string); ok {
		if f, err := strconv.ParseFloat(lat, 64); err == nil && f >= -90 && f <= 90 {
			region["latitude"] = f
		}
	}
	if lon, ok := values["provider_region_longitude"].(string); ok {
		if f, err := strconv.ParseFloat(lon, 64); err == nil && f >= -180 && f <= 180 {
			region["longitude"] = f
		}
	}
	providerData := map[string]interface{}{
		"name": providerName,
		"code": "onprem",
		"details": map[string]interface{}{
			"skipProvisioning": true,
			"cloudInfo": map[string]interface{}{
				"onprem": map[string]interface{}{
					"ybHomeDir":     ybHomeDir,
					"useClockbound": useClockbound,
				},
			},
		},
		"regions": []interface{}{region},
	}
	if keyPath, ok := values["provider_access_key_path"].(string); ok && keyPath != "" {
		keyContent, err := os.ReadFile(keyPath)
		if err != nil {
			return nil, err
		}
		providerData["allAccessKeys"] = []interface{}{
			map[string]interface{}{
				"keyInfo": map[string]interface{}{
					"keyPairName":              fmt.Sprintf("onprem_key_%d.pem", timestamp),
					"sshPrivateKeyContent":     strings.TrimSpace(string(keyContent)),
					"skipKeyValidateAndUpload": false,
				},
			},
		}
	}
	return providerData, nil
}

func (m *InstallNodeAgent) generateProviderUpdatePayload(
	values map[string]any,
	provider map[string]interface{},
) map[string]interface{} {
	regions, _ := provider["regions"].([]interface{})
	regionExist := false
	regionName := values["provider_region_name"].(string)
	zoneName := values["provider_region_zone_name"].(string)
	for _, r := range regions {
		region, _ := r.(map[string]interface{})
		if region["code"] == regionName {
			regionExist = true
			zones, _ := region["zones"].([]interface{})
			zoneExist := false
			for _, z := range zones {
				zone, _ := z.(map[string]interface{})
				if zone["code"] == zoneName {
					zoneExist = true
				}
			}
			if !zoneExist {
				region["zones"] = append(zones, map[string]interface{}{
					"name": zoneName, "code": zoneName,
				})
			}
		}
	}
	if !regionExist {
		regions = append(regions, map[string]interface{}{
			"name": regionName, "code": regionName,
			"zones": []interface{}{map[string]interface{}{"name": zoneName, "code": zoneName}},
		})
	}
	provider["regions"] = regions
	return provider
}

func (m *InstallNodeAgent) generateInstanceTypePayload(
	values map[string]any,
) (map[string]interface{}, error) {
	instanceTypeName := values["instance_type_name"].(string)
	numCores := values["instance_type_cores"]
	memSizeGB := values["instance_type_memory_size"]
	volumeSizeGB := values["instance_type_volume_size"]
	mountPoints, ok := values["instance_type_mount_points"].(string)
	if !ok || mountPoints == "" {
		return nil, errors.New(
			"instance_type_mount_points is required but not provided in configuration",
		)
	}
	mountPoints = strings.Trim(mountPoints, "[]")
	mountPoints = strings.ReplaceAll(mountPoints, "'", "")
	mps := strings.Split(mountPoints, ", ")
	volumeDetails := []interface{}{}
	for _, mp := range mps {
		volumeDetails = append(volumeDetails, map[string]interface{}{
			"volumeSizeGB": volumeSizeGB,
			"volumeType":   "SSD",
			"mountPath":    mp,
		})
	}
	return map[string]interface{}{
		"idKey": map[string]interface{}{
			"instanceTypeCode": instanceTypeName,
		},
		"providerUuid": "$provider_id",
		"providerCode": "onprem",
		"numCores":     numCores,
		"memSizeGB":    memSizeGB,
		"instanceTypeDetails": map[string]interface{}{
			"volumeDetailsList": volumeDetails,
		},
	}, nil
}

func (m *InstallNodeAgent) generateAddNodePayload(values map[string]any) map[string]interface{} {
	return map[string]interface{}{
		"nodes": []interface{}{
			map[string]interface{}{
				"instanceType": values["instance_type_name"],
				"ip":           values["node_external_fqdn"],
				"region":       values["provider_region_name"],
				"zone":         values["provider_region_zone_name"],
				"instanceName": values["node_name"],
			},
		},
	}
}

func (m *InstallNodeAgent) getProvider(values map[string]any) ([]byte, error) {
	providerURL := m.getProviderURL(values)
	ybaURL := values["url"].(string)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaURL), "https")
	headers := m.getHeaders(values["api_key"].(string))
	resp, _, err := m.makeRequest(providerURL, "GET", headers, nil, skipTLSVerify)
	return resp, err
}

func (m *InstallNodeAgent) createInstanceIfNotExists(
	values map[string]any,
	provider map[string]interface{},
) error {
	ybaURL := values["url"].(string)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaURL), "https")
	getInstanceTypeURL := m.getInstanceTypeURL(
		values["url"].(string),
		values["customer_uuid"].(string),
		provider["uuid"].(string),
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
		log.Println("Instance type does not exist, creating it.")
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

func (m *InstallNodeAgent) cleanup(values map[string]any) {
	filesToRemove := []string{
		"create_provider.json",
		"update_provider.json",
		"create_instance.json",
		"add_node_to_provider.json",
	}
	tmpDir, ok := values["tmp_directory"].(string)
	if !ok {
		log.Println("Temporary directory not found")
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
	m.cleanup(values)
	nodeAgentEnabled := false
	resp, err := m.getProvider(values)
	if err != nil {
		return nil, err
	}
	var providerData interface{}
	if err := json.Unmarshal(resp, &providerData); err != nil {
		return nil, err
	}
	var provider map[string]interface{}
	if arr, ok := providerData.([]interface{}); ok && len(arr) > 0 {
		provider = arr[0].(map[string]interface{})
		providerDetails := provider["details"].(map[string]interface{})
		regions, _ := provider["regions"].([]interface{})
		regionExists := false
		zoneExists := false
		regionName := values["provider_region_name"].(string)
		zoneName := values["provider_region_zone_name"].(string)
		for _, r := range regions {
			region, _ := r.(map[string]interface{})
			if region["code"] == regionName {
				regionExists = true
				zones, _ := region["zones"].([]interface{})
				for _, z := range zones {
					zone, _ := z.(map[string]interface{})
					if zone["code"] == zoneName {
						zoneExists = true
					}
				}
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
		if err := m.createInstanceIfNotExists(values, provider); err != nil {
			return nil, err
		}
		values["provider_id"] = fmt.Sprintf("%v", provider["uuid"])
		if en, ok := providerDetails["enableNodeAgent"].(bool); ok {
			nodeAgentEnabled = en
		}
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
	addNodePayload := m.generateAddNodePayload(values)
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
	if nodeAgentEnabled {
		return m.BaseModule.RenderTemplates(ctx, values)
	}
	return nil, nil
}
