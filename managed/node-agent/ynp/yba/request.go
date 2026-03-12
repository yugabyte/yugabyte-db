// Copyright (c) YugabyteDB, Inc.

package yba

import (
	"bytes"
	"context"
	"crypto/tls"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"node-agent/model"
	"node-agent/util"
	"strings"
)

func getAuthHeaders(token string) map[string]string {
	return map[string]string{
		"Accept":              "application/json",
		"X-AUTH-YW-API-TOKEN": token,
		"Content-Type":        "application/json",
	}
}

// MakeRequest makes an HTTP request to the given URL with the specified method, headers, and data.
func MakeRequest(
	ctx context.Context,
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
		util.FileLogger().
			Errorf(ctx, "API response read error for %s URL %s: %v", method, url, err)
		return nil, resp.StatusCode, err
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		util.FileLogger().
			Errorf(ctx, "Status code for %s URL %s: %v", method, url, resp.StatusCode)
		if resp.StatusCode == http.StatusNotFound {
			return nil, resp.StatusCode, util.ErrNotExist
		}
		return respBody, resp.StatusCode, fmt.Errorf("HTTP error: %d", resp.StatusCode)
	}
	return respBody, resp.StatusCode, nil
}

// GetSessionInfo makes an API call to YBA to get the session info for the node agent.
// Session info contains the customer ID.
func GetSessionInfo(
	ctx context.Context,
	ybaUrl, apiKey string,
	skipTlsVerify bool,
) (*model.SessionInfo, error) {
	sessionInfoUrl := ybaUrl + util.PlatformGetSessionInfoEndpoint()
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaUrl), "https") || skipTlsVerify
	headers := getAuthHeaders(apiKey)
	resp, _, err := MakeRequest(ctx, sessionInfoUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var sessionInfo model.SessionInfo
	if err := json.Unmarshal(resp, &sessionInfo); err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to unmarshal session info response: %s, error: %v", string(resp), err)
		return nil, err
	}
	return &sessionInfo, nil
}

// GetRuntimeConfig makes an API call to YBA to get the runtime config value for the given key in the scope.
func GetRuntimeConfig(
	ctx context.Context,
	ybaUrl, apiKey string,
	skipTlsVerify bool,
	customerUuid, scopeUuid, key string,
) (string, error) {
	runtimeConfigUrl := ybaUrl + util.RuntimeConfigEndpoint(customerUuid, scopeUuid, key)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaUrl), "https") || skipTlsVerify
	headers := getAuthHeaders(apiKey)
	resp, _, err := MakeRequest(ctx, runtimeConfigUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return "", err
	}
	return string(resp), nil
}

// GetProviderByZone makes an API call to YBA to get the provider info by zone UUID.
func GetProviderByZone(
	ctx context.Context,
	ybaUrl, apiKey string,
	skipTlsVerify bool,
	customerUuid, zoneUuid string,
) (*model.Provider, error) {
	providerUrl := ybaUrl + util.PlatformGetProvidersEndpoint(customerUuid)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaUrl), "https") || skipTlsVerify
	headers := getAuthHeaders(apiKey)
	resp, _, err := MakeRequest(ctx, providerUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var providers []model.Provider
	if err := json.Unmarshal(resp, &providers); err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to unmarshal provider response: %s, error: %v", string(resp), err)
		return nil, err
	}
	var provider *model.Provider
outer:
	for i := range providers {
		for _, region := range providers[i].Regions {
			for _, zone := range region.Zones {
				if zone.Uuid == zoneUuid {
					provider = &providers[i]
					break outer
				}
			}
		}
	}
	if provider == nil {
		util.FileLogger().
			Errorf(ctx, "Provider with zone %s is not found for customer %s", zoneUuid, customerUuid)
		return nil, util.ErrNotExist
	}
	return provider, err
}

// GetProviderByName makes an API call to YBA to get the provider info by provider name.
func GetProviderByName(
	ctx context.Context,
	ybaUrl, apiKey string,
	skipTlsVerify bool,
	customerUuid, providerName string,
) (*model.Provider, error) {
	providerUrl := ybaUrl + util.PlatformGetProviderByNameEndpoint(customerUuid, providerName)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaUrl), "https") || skipTlsVerify
	headers := getAuthHeaders(apiKey)
	resp, _, err := MakeRequest(ctx, providerUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var providers []model.Provider
	if err := json.Unmarshal(resp, &providers); err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to unmarshal provider response: %s, error: %v", string(resp), err)
		return nil, err
	}
	if len(providers) == 0 {
		util.FileLogger().
			Errorf(ctx, "Provider %s is not found for customer %s", providerName, customerUuid)
		return nil, util.ErrNotExist
	}
	return &providers[0], err
}

// GetInstanceType makes an API call to YBA to get the instance type info by instance type code.
func GetInstanceType(
	ctx context.Context,
	ybaUrl, apiKey string,
	skipTlsVerify bool,
	customerUuid, providerUuid, instanceTypeCode string,
) (*model.NodeInstanceType, error) {
	instanceTypeUrl := ybaUrl + util.PlatformGetInstanceTypeEndpoint(
		customerUuid,
		providerUuid,
		instanceTypeCode,
	)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaUrl), "https") || skipTlsVerify
	headers := getAuthHeaders(apiKey)
	resp, _, err := MakeRequest(ctx, instanceTypeUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var instanceType model.NodeInstanceType
	if err := json.Unmarshal(resp, &instanceType); err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to unmarshal instance type response: %s, error: %v", string(resp), err)
		return nil, err
	}
	return &instanceType, nil
}

// GetNodeInstanceByIp makes an API call to YBA to get the node instance info by node IP or FQDN.
func GetNodeInstanceByIp(
	ctx context.Context,
	ybaUrl, apiKey string,
	skipTlsVerify bool,
	customerUuid, nodeIp string,
) (*model.NodeInstance, error) {
	providerNodesUrl := ybaUrl + util.PlatformGetNodeInstanceByIpEndpoint(customerUuid, nodeIp)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaUrl), "https") || skipTlsVerify
	headers := getAuthHeaders(apiKey)
	resp, _, err := MakeRequest(ctx, providerNodesUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var instances []model.NodeInstance
	if err := json.Unmarshal(resp, &instances); err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to unmarshal node instance response: %s, error: %v", string(resp), err)
		return nil, err
	}
	if len(instances) == 0 {
		util.FileLogger().
			Errorf(ctx, "Node instance with IP %s is not found for customer %s", nodeIp, customerUuid)
		return nil, util.ErrNotExist
	}
	return &instances[0], nil
}

// GetProviderNodeInstances makes an API call to YBA to get the list of node instances for a provider.
func GetProviderNodeInstances(
	ctx context.Context,
	ybaUrl, apiKey string,
	skipTlsVerify bool,
	customerUuid, providerUuid string,
) ([]model.NodeInstance, error) {
	providerNodesUrl := ybaUrl + util.PlatformGetNodeInstancesEndpoint(customerUuid, providerUuid)
	skipTLSVerify := !strings.HasPrefix(strings.ToLower(ybaUrl), "https") || skipTlsVerify
	headers := getAuthHeaders(apiKey)
	resp, _, err := MakeRequest(ctx, providerNodesUrl, "GET", headers, nil, skipTLSVerify)
	if err != nil {
		return nil, err
	}
	var instances []model.NodeInstance
	if err := json.Unmarshal(resp, &instances); err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to unmarshal provider node instances response: %s, error: %v", string(resp), err)
		return nil, err
	}
	return instances, nil
}
