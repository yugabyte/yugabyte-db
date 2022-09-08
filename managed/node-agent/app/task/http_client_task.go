// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"encoding/json"
	"errors"
	"io/ioutil"
	"net/http"
	"node-agent/model"
	"node-agent/util"
	"strings"
)

const (
	mountPoints   = "mount_points"
	portAvailable = "ports"
)

func httpClient() *util.HttpClient {
	config := util.CurrentConfig()
	return util.NewHttpClient(
		config.Int(util.RequestTimeoutKey),
		config.String(util.PlatformUrlKey),
	)
}

type AgentRegistrationHandler struct {
	apiToken string
	result   *model.RegisterResponseSuccess
}

func NewAgentRegistrationHandler(apiToken string) *AgentRegistrationHandler {
	return &AgentRegistrationHandler{apiToken: apiToken}
}

func (handler *AgentRegistrationHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	// Call the platform to register the node-agent in the platform.
	res, err := httpClient().Do(
		http.MethodPost,
		util.PlatformRegisterAgentEndpoint(config.String(util.CustomerIdKey)),
		platformHeadersWithAPIToken(handler.apiToken),
		nil,
		createRegisterAgentRequest(config),
	)
	if err != nil {
		return nil, err
	}
	handler.result = &model.RegisterResponseSuccess{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *AgentRegistrationHandler) Result() *model.RegisterResponseSuccess {
	return handler.result
}

type AgentUnregistrationHandler struct {
	useJWT   bool
	apiToken string
	result   *model.RegisterResponseEmpty
}

func NewAgentUnregistrationHandler(useJWT bool, apiToken string) *AgentUnregistrationHandler {
	return &AgentUnregistrationHandler{useJWT: useJWT, apiToken: apiToken}
}

func (handler *AgentUnregistrationHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	var headers map[string]string
	var err error
	// Call the platform to unregister the node-agent in the platform.
	// Check whether to use JWT or Api Token to call the api.
	if handler.useJWT {
		headers, err = platformHeadersWithJWT(config)
		if err != nil {
			return nil, err
		}
	} else {
		headers = platformHeadersWithAPIToken(handler.apiToken)
	}
	res, err := httpClient().Do(
		http.MethodDelete,
		util.PlatformUnregisterAgentEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.NodeAgentIdKey),
		),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	handler.result = &model.RegisterResponseEmpty{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *AgentUnregistrationHandler) Result() *model.RegisterResponseEmpty {
	return handler.result
}

type GetPlatformCurrentConfigHandler struct {
	result *model.NodeInstanceType
}

func NewGetPlatformCurrentConfigHandler() *GetPlatformCurrentConfigHandler {
	return &GetPlatformCurrentConfigHandler{}
}

func (handler *GetPlatformCurrentConfigHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetConfigEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.ProviderIdKey),
			config.String(util.NodeInstanceTypeKey),
		),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	handler.result = &model.NodeInstanceType{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetPlatformCurrentConfigHandler) Result() *model.NodeInstanceType {
	return handler.result
}

type SendNodeCapabilityHandler struct {
	data   map[string]model.PreflightCheckVal
	result *map[string]model.NodeCapabilityResponse
}

func NewSendNodeCapabilityHandler(
	data map[string]model.PreflightCheckVal,
) *SendNodeCapabilityHandler {
	return &SendNodeCapabilityHandler{data: data}
}

func (handler *SendNodeCapabilityHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodPost,
		util.PlatformPostNodeCapabilitiesEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.NodeAzIdKey),
		),
		headers,
		nil,
		createNodeCapabilitesRequest(config, handler.data),
	)
	if err != nil {
		return nil, err
	}
	handler.result = &map[string]model.NodeCapabilityResponse{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *SendNodeCapabilityHandler) Result() *map[string]model.NodeCapabilityResponse {
	return handler.result
}

type GetProvidersHandler struct {
	apiToken string
	result   *[]model.Provider
}

func NewGetProvidersHandler(apiToken string) *GetProvidersHandler {
	return &GetProvidersHandler{apiToken: apiToken}
}

func (handler *GetProvidersHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers := platformHeadersWithAPIToken(handler.apiToken)
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetProvidersEndpoint(config.String(util.CustomerIdKey)),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	handler.result = &[]model.Provider{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetProvidersHandler) Result() *[]model.Provider {
	return handler.result
}

type GetSessionInfoHandler struct {
	apiToken string
	result   *model.SessionInfo
}

func NewGetSessionInfoHandler(apiToken string) *GetSessionInfoHandler {
	return &GetSessionInfoHandler{apiToken: apiToken}
}

func (handler *GetSessionInfoHandler) Handle(ctx context.Context) (any, error) {
	headers := platformHeadersWithAPIToken(handler.apiToken)
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetSessionInfoEndpoint(),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	handler.result = &model.SessionInfo{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetSessionInfoHandler) Result() *model.SessionInfo {
	return handler.result
}

type GetUserHandler struct {
	apiToken string
	result   *model.User
}

func NewGetUserHandler(apiToken string) *GetUserHandler {
	return &GetUserHandler{apiToken: apiToken}
}

func (handler *GetUserHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers := platformHeadersWithAPIToken(handler.apiToken)
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetUserEndpoint(config.String(util.CustomerIdKey), config.String(util.UserIdKey)),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	handler.result = &model.User{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetUserHandler) Result() *model.User {
	return handler.result
}

type GetInstanceTypesHandler struct {
	apiToken string
	result   *[]model.NodeInstanceType
}

func NewGetInstanceTypesHandler(apiToken string) *GetInstanceTypesHandler {
	return &GetInstanceTypesHandler{apiToken: apiToken}
}

func (handler *GetInstanceTypesHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers := platformHeadersWithAPIToken(handler.apiToken)
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetInstanceTypesEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.ProviderIdKey),
		),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	handler.result = &[]model.NodeInstanceType{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetInstanceTypesHandler) Result() *[]model.NodeInstanceType {
	return handler.result
}

type GetAgentStateHandler struct {
	result *string
}

func NewGetAgentStateHandler() *GetAgentStateHandler {
	return &GetAgentStateHandler{}
}

func (handler *GetAgentStateHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetAgentStateEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.NodeAgentIdKey),
		),
		nil,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	var state string
	handler.result = &state
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetAgentStateHandler) Result() *string {
	return handler.result
}

type PutAgentStateHandler struct {
	state   model.NodeState
	version string
	result  *model.NodeAgent
}

func NewPutAgentStateHandler(state model.NodeState, version string) *PutAgentStateHandler {
	return &PutAgentStateHandler{state: state, version: version}
}

func (handler *PutAgentStateHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodPut,
		util.PlatformPutAgentStateEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.NodeAgentIdKey),
		),
		headers,
		nil,
		createUpdateAgentStateRequest(config, handler.state, handler.version),
	)
	if err != nil {
		return nil, err
	}
	handler.result = &model.NodeAgent{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *PutAgentStateHandler) Result() *model.NodeAgent {
	return handler.result
}

type PutAgentHandler struct {
	result *model.NodeAgent
}

func NewPutAgentHandler() *PutAgentHandler {
	return &PutAgentHandler{}
}

func (handler *PutAgentHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodPut,
		util.PlatformPutAgentEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.NodeAgentIdKey),
		),
		headers,
		nil,
		createUpdateAgentRequest(config),
	)
	if err != nil {
		return nil, err
	}
	handler.result = &model.NodeAgent{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *PutAgentHandler) Result() *model.NodeAgent {
	return handler.result
}

type GetVersionHandler struct {
	result *model.VersionRequest
}

func NewGetVersionHandler() *GetVersionHandler {
	return &GetVersionHandler{}
}

func (handler *GetVersionHandler) Handle(ctx context.Context) (any, error) {
	res, err := httpClient().Do(http.MethodGet, util.GetVersionEndpoint, nil, nil, nil)
	if err != nil {
		return nil, err
	}
	handler.result = &model.VersionRequest{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetVersionHandler) Result() *model.VersionRequest {
	return handler.result
}

// Unmarshals the response body to the provided target.
// Tries to unmarshal the response into model.ResponseError if
// the response status code is not 200.
// If the unmarshaling fails, converts the response body to string.
func UnmarshalResponse(successTarget any, res *http.Response) (any, error) {
	body, err := ioutil.ReadAll(res.Body)
	res.Body.Close()
	if err != nil {
		err = errors.New("Error reading the response body - " + err.Error())
		util.FileLogger().Errorf(err.Error())
		return nil, err
	}
	if res.StatusCode != 200 {
		util.FileLogger().Errorf(
			"API returned an error %s with %d status code",
			string(body),
			res.StatusCode,
		)
		var failTarget model.ResponseError
		err = json.Unmarshal(body, &failTarget)
		if err == nil {
			return nil, errors.New(failTarget.Error())
		}

		// Unmarshal the error response into a string.
		errStr := string(body)
		return nil, errors.New(errStr)
	}
	err = json.Unmarshal(body, successTarget)
	if err != nil {
		err = errors.New("Error while unmarshaling the response body - " + err.Error())
		util.FileLogger().Errorf(err.Error())
		return nil, err
	}
	return successTarget, nil
}

// Creates Platform headers using JWT.
func platformHeadersWithJWT(config *util.Config) (map[string]string, error) {
	m := make(map[string]string)
	m["Content-Type"] = "application/json"
	jwtToken, err := util.GenerateJWT(config)
	if err != nil {
		util.FileLogger().Errorf("Error while creating the JWT - %s", err.Error())
		return m, err
	}
	m[util.PlatformJwtTokenHeader] = jwtToken
	return m, nil
}

// Creates Platform headers using API Token.
func platformHeadersWithAPIToken(apiToken string) map[string]string {
	m := make(map[string]string)
	m["Content-Type"] = "application/json"
	m[util.PlatformApiTokenHeader] = apiToken
	return m
}

func createRegisterAgentRequest(config *util.Config) model.RegisterRequest {
	req := model.RegisterRequest{}
	req.Name = config.String(util.NodeNameKey)
	req.IP = config.String(util.NodeIpKey)
	req.Version = config.String(util.PlatformVersionKey)
	req.State = model.Registering.Name()
	return req
}

func createUpdateAgentStateRequest(
	config *util.Config,
	state model.NodeState,
	version string,
) model.StateUpdateRequest {
	req := model.StateUpdateRequest{}
	req.Name = config.String(util.NodeNameKey)
	req.IP = config.String(util.NodeIpKey)
	req.Version = version
	req.State = state.Name()
	return req
}

func createUpdateAgentRequest(config *util.Config) model.StateUpdateRequest {
	req := model.StateUpdateRequest{}
	req.Name = config.String(util.NodeNameKey)
	req.IP = config.String(util.NodeIpKey)
	req.Version = config.String(util.PlatformVersionKey)
	req.State = model.Upgrading.Name()
	return req
}

func createNodeCapabilitesRequest(
	config *util.Config,
	data map[string]model.PreflightCheckVal,
) model.NodeCapabilityRequest {
	req := model.NodeCapabilityRequest{}
	nodeDetails := model.NodeDetails{}
	nodeDetails.IP = config.String(util.NodeIpKey)
	nodeDetails.Region = config.String(util.NodeRegionKey)
	nodeDetails.Zone = config.String(util.NodeZoneKey)
	nodeDetails.InstanceType = config.String(util.NodeInstanceTypeKey)
	nodeDetails.InstanceName = config.String(util.NodeInstanceNameKey)
	nodeDetails.NodeConfigs = getNodeConfig(data)
	nodeDetailsList := [...]model.NodeDetails{nodeDetails}
	req.Nodes = nodeDetailsList[:]
	return req
}

func getNodeConfig(data map[string]model.PreflightCheckVal) []model.NodeConfig {
	mountPointsMap := make(map[string]string)
	portsMap := make(map[string]string)
	result := make([]model.NodeConfig, 0)
	for k, v := range data {
		if v.Error == "none" {
			kSplit := strings.Split(k, ":")
			switch kSplit[0] {
			case mountPoints:
				mountPointsMap[kSplit[1]] = v.Value
			case portAvailable:
				portsMap[kSplit[1]] = v.Value
			default:
				// Try Getting Python Version.
				vSplit := strings.Split(v.Value, " ")
				if len(vSplit) > 0 && strings.EqualFold(vSplit[0], "Python") {
					result = append(
						result,
						model.NodeConfig{Type: strings.ToUpper(kSplit[0]), Value: vSplit[1]},
					)
				} else {
					result = append(result, model.NodeConfig{Type: strings.ToUpper(kSplit[0]), Value: v.Value})
				}
			}
		}
	}

	// Marshal the mount points in the request.
	if len(mountPointsMap) > 0 {
		mountPointsJson, err := json.Marshal(mountPointsMap)
		if err != nil {
			panic("Error while marshaling mount points map")
		}
		result = append(
			result,
			model.NodeConfig{Type: strings.ToUpper(mountPoints), Value: string(mountPointsJson)},
		)
	}

	// Marshal the ports in the request.
	if len(portsMap) > 0 {
		portsJson, err := json.Marshal(portsMap)
		if err != nil {
			panic("Error while marshaling ports map")
		}
		result = append(
			result,
			model.NodeConfig{Type: strings.ToUpper(portAvailable), Value: string(portsJson)},
		)
	}

	return result
}
