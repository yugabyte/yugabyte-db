/*
 * Copyright (c) YugaByte, Inc.
 */
package task

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"net/http"
	"node-agent/model"
	"node-agent/util"
	"strings"
)

const (
	mountPoint    = "mount_point"
	portAvailable = "ports"
)

var (
	config     *util.Config
	httpClient *util.HttpClient
)

func InitHttpClient(httpConfig *util.Config) {
	config = httpConfig
	httpClient = util.NewHttpClient(
		config.GetInt(util.RequestTimeout),
		config.GetString(util.PlatformHost),
		config.GetString(util.PlatformPort),
	)
}

func HandleAgentRegistration(apiToken string) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		//Call the platform to register the node-agent in the platform.
		res, err := httpClient.Do(
			http.MethodPost,
			util.PlatformRegisterAgentEndpoint(config.GetString(util.CustomerId)),
			getPlatformHeadersWithAPIToken(apiToken),
			nil,
			createRegisterAgentRequest(config),
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&model.RegisterResponseSuccess{}, &model.ResponseError{}, res)
	}
}

func HandleAgentUnregister(useJWT bool, apiToken string) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		var headers map[string]string
		var err error
		//Call the platform to unregister the node-agent in the platform.
		//Check whether to use JWT or Api Token to call the api.
		if useJWT {
			headers, err = getPlatformHeadersWithJWT(config)
			if err != nil {
				return nil, err
			}
		} else {
			headers = getPlatformHeadersWithAPIToken(apiToken)
		}
		res, err := httpClient.Do(
			http.MethodDelete,
			util.PlatformUnregisterAgentEndpoint(
				config.GetString(util.CustomerId),
				config.GetString(util.NodeAgentId),
			),
			headers,
			nil,
			nil,
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&model.RegisterResponseEmpty{}, &model.ResponseError{}, res)
	}
}

func HandleGetPlatformConfig() func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers, err := getPlatformHeadersWithJWT(config)
		if err != nil {
			return nil, err
		}
		res, err := httpClient.Do(
			http.MethodGet,
			util.PlatformGetConfigEndpoint(
				config.GetString(util.CustomerId),
				config.GetString(util.ProviderId),
				config.GetString(util.NodeInstanceType),
			),
			headers,
			nil,
			nil,
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&model.NodeInstanceType{}, &model.ResponseError{}, res)
	}
}

func HandleSendNodeCapability(
	data map[string]model.PreflightCheckVal,
) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers, err := getPlatformHeadersWithJWT(config)
		if err != nil {
			return nil, err
		}
		res, err := httpClient.Do(
			http.MethodPost,
			util.PlatformPostNodeCapabilitiesEndpoint(
				config.GetString(util.CustomerId),
				config.GetString(util.NodeAzId),
			),
			headers,
			nil,
			createNodeCapabilitesRequest(config, data),
		)
		if err != nil {
			return nil, err
		}
		var result map[string]model.NodeCapabilityResponse
		return UnmarshalResponse(&result, &model.ResponseError{}, res)
	}
}

func HandleGetCustomers(apiToken string) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers := getPlatformHeadersWithAPIToken(apiToken)
		res, err := httpClient.Do(http.MethodGet, util.GetCustomersApiEndpoint, headers, nil, nil)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&[]model.Customer{}, &model.ResponseError{}, res)
	}
}

func HandleGetProviders(apiToken string) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers := getPlatformHeadersWithAPIToken(apiToken)
		res, err := httpClient.Do(
			http.MethodGet,
			util.PlatformGetProvidersEndpoint(config.GetString(util.CustomerId)),
			headers,
			nil,
			nil,
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&[]model.Provider{}, &model.ResponseError{}, res)
	}
}

func HandleGetUsers(apiToken string) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers := getPlatformHeadersWithAPIToken(apiToken)
		res, err := httpClient.Do(
			http.MethodGet,
			util.PlatformGetUsersEndpoint(config.GetString(util.CustomerId)),
			headers,
			nil,
			nil,
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&[]model.User{}, &model.ResponseError{}, res)
	}
}

func HandleGetInstanceTypes(apiToken string) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers := getPlatformHeadersWithAPIToken(apiToken)
		res, err := httpClient.Do(
			http.MethodGet,
			util.PlatformGetInstanceTypesEndpoint(
				config.GetString(util.CustomerId),
				config.GetString(util.ProviderId),
			),
			headers,
			nil,
			nil,
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&[]model.NodeInstanceType{}, &model.ResponseError{}, res)
	}
}

func HandleGetAgentState() func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		res, err := httpClient.Do(
			http.MethodGet,
			util.PlatformGetAgentStateEndpoint(
				config.GetString(util.CustomerId),
				config.GetString(util.NodeAgentId),
			),
			nil,
			nil,
			nil,
		)
		if err != nil {
			return nil, err
		}
		var state string
		return UnmarshalResponse(&state, &model.ResponseError{}, res)
	}
}

func HandlePutAgentState(
	state model.NodeState,
	versionToSend string,
) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers, err := getPlatformHeadersWithJWT(config)
		if err != nil {
			return nil, err
		}
		res, err := httpClient.Do(
			http.MethodPut,
			util.PlatformPutAgentStateEndpoint(
				config.GetString(util.CustomerId),
				config.GetString(util.NodeAgentId),
			),
			headers,
			nil,
			createUpdateAgentStateRequest(config, state, versionToSend),
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&model.NodeAgent{}, &model.ResponseError{}, res)
	}
}

func HandlePutAgent() func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers, err := getPlatformHeadersWithJWT(config)
		if err != nil {
			return nil, err
		}
		res, err := httpClient.Do(
			http.MethodPut,
			util.PlatformPutAgentEndpoint(
				config.GetString(util.CustomerId),
				config.GetString(util.NodeAgentId),
			),
			headers,
			nil,
			createUpdateAgentRequest(config),
		)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&model.NodeAgent{}, &model.ResponseError{}, res)
	}
}

func HandleGetVersion() func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		res, err := httpClient.Do(http.MethodGet, util.GetVersionEndpoint, nil, nil, nil)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&model.VersionRequest{}, &model.ResponseError{}, res)
	}
}

//Unmarshals the response body to the provided target.
//Takes Expected Success and Fail targets.
//Returns pointer to the target.
func UnmarshalResponse(successTarget any, failTarget error, res *http.Response) (any, error) {
	body, err := ioutil.ReadAll(res.Body)
	res.Body.Close()
	if err != nil {
		errStr := errors.New("Error reading the response body")
		util.FileLogger.Errorf(err.Error())
		return nil, errStr
	}
	if res.StatusCode != 200 {
		util.FileLogger.Errorf(
			"API returned an error %s with %d status code",
			string(body),
			res.StatusCode,
		)
		err = json.Unmarshal(body, failTarget)
		if err == nil {
			return failTarget, errors.New(res.Status)
		}

		//Unmarshal the error response into a string
		errStr := string(body)
		return &errStr, fmt.Errorf("%s %s", res.Status, failTarget.Error())
	}
	err = json.Unmarshal(body, successTarget)
	if err != nil {
		errStr := errors.New("Error while unmarshaling the response body - " + err.Error())
		util.FileLogger.Errorf(errStr.Error())
		return nil, errStr
	}
	return successTarget, nil
}

//Creates Platform headers using JWT
func getPlatformHeadersWithJWT(config *util.Config) (map[string]string, error) {
	m := make(map[string]string)
	m["Content-Type"] = "application/json"
	jwtToken, err := util.GenerateJWT(config)
	if err != nil {
		util.FileLogger.Errorf("Error while creating the JWT - %s", err.Error())
		return m, err
	}
	m[util.PlatformJwtTokenHeader] = jwtToken
	return m, nil
}

//Creates Platform headers using API Token
func getPlatformHeadersWithAPIToken(apiToken string) map[string]string {
	m := make(map[string]string)
	m["Content-Type"] = "application/json"
	m[util.PlatformApiTokenHeader] = apiToken
	return m
}

func createRegisterAgentRequest(config *util.Config) model.RegisterRequest {
	req := model.RegisterRequest{}
	req.Name = config.GetString(util.NodeName)
	req.IP = config.GetString(util.NodeIP)
	req.Version = config.GetString(util.PlatformVersion)
	req.State = model.Registering.Name()
	return req
}

func createUpdateAgentStateRequest(
	config *util.Config,
	state model.NodeState,
	version string,
) model.StateUpdateRequest {
	req := model.StateUpdateRequest{}
	req.Name = config.GetString(util.NodeName)
	req.IP = config.GetString(util.NodeIP)
	req.Version = version
	req.State = state.Name()
	return req
}

func createUpdateAgentRequest(config *util.Config) model.StateUpdateRequest {
	req := model.StateUpdateRequest{}
	req.Name = config.GetString(util.NodeName)
	req.IP = config.GetString(util.NodeIP)
	req.Version = config.GetString(util.PlatformVersion)
	req.State = model.Upgrading.Name()
	return req
}

func createNodeCapabilitesRequest(
	config *util.Config,
	data map[string]model.PreflightCheckVal,
) model.NodeCapabilityRequest {
	req := model.NodeCapabilityRequest{}
	nodeDetails := model.NodeDetails{}
	nodeDetails.IP = config.GetString(util.NodeIP)
	nodeDetails.Region = config.GetString(util.NodeRegion)
	nodeDetails.Zone = config.GetString(util.NodeZone)
	nodeDetails.InstanceType = config.GetString(util.NodeInstanceType)
	nodeDetails.InstanceName = config.GetString(util.NodeInstanceName)
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
			k_split := strings.Split(k, ":")
			if len(k_split) == 0 {
				result = append(result, model.NodeConfig{Type: strings.ToUpper(k), Value: v.Value})
			} else {
				switch k_split[0] {
				case mountPoint:
					mountPointsMap[k_split[1]] = v.Value
				case portAvailable:
					portsMap[k_split[1]] = v.Value
				default:
					result = append(result, model.NodeConfig{Type: strings.ToUpper(k_split[0]), Value: v.Value})
				}
			}
		}
	}

	//Marshal the mount points in the request.
	if len(mountPointsMap) > 0 {
		mountPointsJson, err := json.Marshal(mountPointsMap)
		if err != nil {
			panic("Error while marshaling mount points map")
		}
		result = append(
			result,
			model.NodeConfig{Type: strings.ToUpper(mountPoint), Value: string(mountPointsJson)},
		)
	}

	//Marshal the ports in the request.
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
