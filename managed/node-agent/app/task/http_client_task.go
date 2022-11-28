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
	"sort"
	"strings"
)

const (
	mountPointsVolume    = "mount_points_volume"
	mountPointsWritable  = "mount_points_writable"
	masterHTTPPort       = "master_http_port"
	masterRPCPort        = "master_rpc_port"
	tserverHTTPPort      = "tserver_http_port"
	tserverRPCPort       = "tserver_rpc_port"
	ybControllerHTTPPort = "yb_controller_http_port"
	ybControllerRPCPort  = "yb_controller_rpc_port"
	redisServerHTTPPort  = "redis_server_http_port"
	redisServerRPCPort   = "redis_server_rpc_port"
	yqlServerHTTPPort    = "yql_server_http_port"
	yqlServerRPCPort     = "yql_server_rpc_port"
	ysqlServerHTTPPort   = "ysql_server_http_port"
	ysqlServerRPCPort    = "ysql_server_rpc_port"
	sshPort              = "ssh_port"
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
	defer res.Body.Close()
	handler.result = &model.RegisterResponseSuccess{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *AgentRegistrationHandler) Result() *model.RegisterResponseSuccess {
	return handler.result
}

type AgentUnregistrationHandler struct {
	apiToken string
	result   *model.ResponseMessage
}

func NewAgentUnregistrationHandler(apiToken string) *AgentUnregistrationHandler {
	return &AgentUnregistrationHandler{apiToken: apiToken}
}

func (handler *AgentUnregistrationHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithAuth(config, handler.apiToken)
	if err != nil {
		return nil, err
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
	defer res.Body.Close()
	handler.result = &model.ResponseMessage{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *AgentUnregistrationHandler) Result() *model.ResponseMessage {
	return handler.result
}

type GetInstanceTypeHandler struct {
	result *model.NodeInstanceType
}

func NewGetInstanceTypeHandler() *GetInstanceTypeHandler {
	return &GetInstanceTypeHandler{}
}

func (handler *GetInstanceTypeHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetInstanceTypeEndpoint(
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
	defer res.Body.Close()
	handler.result = &model.NodeInstanceType{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetInstanceTypeHandler) Result() *model.NodeInstanceType {
	return handler.result
}

type ValidateNodeInstanceHandler struct {
	data   map[string]model.PreflightCheckVal
	result *map[string]model.NodeInstanceValidationResponse
}

func NewValidateNodeInstanceHandler(
	data map[string]model.PreflightCheckVal,
) *ValidateNodeInstanceHandler {
	return &ValidateNodeInstanceHandler{data: data}
}

func (handler *ValidateNodeInstanceHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodPost,
		util.PlatformValidateNodeInstanceEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.NodeAzIdKey),
		),
		headers,
		nil,
		createNodeDetailsRequest(config, handler.data),
	)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	handler.result = &map[string]model.NodeInstanceValidationResponse{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *ValidateNodeInstanceHandler) Result() *map[string]model.NodeInstanceValidationResponse {
	return handler.result
}

type PostNodeInstanceHandler struct {
	data   map[string]model.PreflightCheckVal
	result *map[string]model.NodeInstanceResponse
}

func NewPostNodeInstanceHandler(
	data map[string]model.PreflightCheckVal,
) *PostNodeInstanceHandler {
	return &PostNodeInstanceHandler{data: data}
}

func (handler *PostNodeInstanceHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodPost,
		util.PlatformPostNodeInstancesEndpoint(
			config.String(util.CustomerIdKey),
			config.String(util.NodeAzIdKey),
		),
		headers,
		nil,
		createNodeInstancesRequest(config, handler.data),
	)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	handler.result = &map[string]model.NodeInstanceResponse{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *PostNodeInstanceHandler) Result() *map[string]model.NodeInstanceResponse {
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
	headers, err := platformHeadersWithAuth(config, handler.apiToken)
	if err != nil {
		return nil, err
	}
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
	defer res.Body.Close()
	handler.result = &[]model.Provider{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetProvidersHandler) Result() *[]model.Provider {
	return handler.result
}

type GetProviderHandler struct {
	result *model.Provider
}

func NewGetProviderHandler() *GetProviderHandler {
	return &GetProviderHandler{}
}

func (handler *GetProviderHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetProviderEndpoint(config.String(util.CustomerIdKey), config.String(util.ProviderIdKey)),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	handler.result = &model.Provider{}
	return UnmarshalResponse(handler.result, res)
}

func (handler *GetProviderHandler) Result() *model.Provider {
	return handler.result
}

type GetAccessKeysHandler struct {
	result *model.AccessKey
}

func NewGetAccessKeysHandler() *GetAccessKeysHandler {
	return &GetAccessKeysHandler{}
}

func (handler *GetAccessKeysHandler) Handle(ctx context.Context) (any, error) {
	config := util.CurrentConfig()
	headers, err := platformHeadersWithJWT(config)
	if err != nil {
		return nil, err
	}
	res, err := httpClient().Do(
		http.MethodGet,
		util.PlatformGetAccessKeysEndpoint(config.String(util.CustomerIdKey), config.String(util.ProviderIdKey)),
		headers,
		nil,
		nil,
	)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	accessKeys := []model.AccessKey{}
	_, err = UnmarshalResponse(&accessKeys, res)
	if err != nil {
		return nil, err
	}
	// Sort to get the latest access key as done in platform.
	sort.Sort(model.AccessKeys(accessKeys))
	handler.result = &accessKeys[0]
	return handler.result, nil
}

func (handler *GetAccessKeysHandler) Result() *model.AccessKey {
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
	config := util.CurrentConfig()
	headers, err := platformHeadersWithAuth(config, handler.apiToken)
	if err != nil {
		return nil, err
	}
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
	defer res.Body.Close()
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
	headers, err := platformHeadersWithAuth(config, handler.apiToken)
	if err != nil {
		return nil, err
	}
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
	defer res.Body.Close()
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
	headers, err := platformHeadersWithAuth(config, handler.apiToken)
	if err != nil {
		return nil, err
	}
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
	defer res.Body.Close()
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
	defer res.Body.Close()
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
	defer res.Body.Close()
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
	defer res.Body.Close()
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
	defer res.Body.Close()
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

// Creates platform headers with either the API token if it is set or JWT token.
func platformHeadersWithAuth(config *util.Config, apiToken string) (map[string]string, error) {
	apiToken = strings.TrimSpace(apiToken)
	if apiToken == "" {
		return platformHeadersWithJWT(config)
	}
	return platformHeadersWithAPIToken(apiToken), nil
}

// Creates platform headers using JWT.
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

// Creates platform headers using API Token.
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
	req.Port = config.Int(util.NodePortKey)
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

func createNodeDetailsRequest(
	config *util.Config,
	data map[string]model.PreflightCheckVal,
) model.NodeDetails {
	nodeDetails := model.NodeDetails{}
	nodeDetails.IP = config.String(util.NodeIpKey)
	nodeDetails.Region = config.String(util.NodeRegionKey)
	nodeDetails.Zone = config.String(util.NodeZoneKey)
	nodeDetails.InstanceType = config.String(util.NodeInstanceTypeKey)
	nodeDetails.InstanceName = config.String(util.NodeInstanceNameKey)
	nodeDetails.NodeConfigs = getNodeConfig(data)
	return nodeDetails
}

func createNodeInstancesRequest(
	config *util.Config,
	data map[string]model.PreflightCheckVal,
) model.NodeInstances {
	return model.NodeInstances{Nodes: []model.NodeDetails{createNodeDetailsRequest(config, data)}}
}

func getNodeConfig(data map[string]model.PreflightCheckVal) []model.NodeConfig {
	mountPointsWritableMap := make(map[string]string)
	mountPointsVolumeMap := make(map[string]string)
	result := make([]model.NodeConfig, 0)
	for k, v := range data {
		kSplit := strings.Split(k, ":")
		switch kSplit[0] {
		case mountPointsWritable:
			mountPointsWritableMap[kSplit[1]] = v.Value
		case mountPointsVolume:
			mountPointsVolumeMap[kSplit[1]] = v.Value
		case masterHTTPPort, masterRPCPort, tserverHTTPPort, tserverRPCPort,
			ybControllerHTTPPort, ybControllerRPCPort, redisServerHTTPPort,
			redisServerRPCPort, yqlServerHTTPPort, yqlServerRPCPort,
			ysqlServerHTTPPort, ysqlServerRPCPort, sshPort:
			portMap := make(map[string]string)
			portMap[kSplit[1]] = v.Value
			result = appendMap(kSplit[0], portMap, result)
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

	// Marshal the existence of mount points in the request.
	result = appendMap(mountPointsWritable, mountPointsWritableMap, result)

	// Marshal the mount points volume in the request.
	result = appendMap(mountPointsVolume, mountPointsVolumeMap, result)

	return result
}

// Marshal helper function for maps.
func appendMap(key string, valMap map[string]string, result []model.NodeConfig) []model.NodeConfig {
	if len(valMap) > 0 {
		valJSON, err := json.Marshal(valMap)
		if err != nil {
			panic("Error while marshaling map")
		}
		return append(
			result,
			model.NodeConfig{
				Type:  strings.ToUpper(key),
				Value: string(valJSON),
			},
		)
	}

	return result
}
