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
		return UnmarshalResponse(&model.RegisterResponseSuccess{}, res)
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
		return UnmarshalResponse(&model.RegisterResponseEmpty{}, res)
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
		return UnmarshalResponse(&model.NodeInstanceType{}, res)
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
		return UnmarshalResponse(&result, res)
	}
}

func HandleGetCustomers(apiToken string) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		headers := getPlatformHeadersWithAPIToken(apiToken)
		res, err := httpClient.Do(http.MethodGet, util.GetCustomersApiEndpoint, headers, nil, nil)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&[]model.Customer{}, res)
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
		return UnmarshalResponse(&[]model.Provider{}, res)
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
		return UnmarshalResponse(&[]model.User{}, res)
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
		return UnmarshalResponse(&[]model.NodeInstanceType{}, res)
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
		return UnmarshalResponse(&state, res)
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
		return UnmarshalResponse(&model.NodeAgent{}, res)
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
		return UnmarshalResponse(&model.NodeAgent{}, res)
	}
}

func HandleGetVersion() func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		res, err := httpClient.Do(http.MethodGet, util.GetVersionEndpoint, nil, nil, nil)
		if err != nil {
			return nil, err
		}
		return UnmarshalResponse(&model.VersionRequest{}, res)
	}
}

//Unmarshals the response body to the provided target.
//Tries to unmarshal the response into model.ResponseError if
//the response status code is not 200.
//If the unmarshaling fails, converts the response body to string.
func UnmarshalResponse(successTarget any, res *http.Response) (any, error) {
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
		var failTarget model.ResponseError
		err = json.Unmarshal(body, &failTarget)
		if err == nil {
			return nil, errors.New(failTarget.Error())
		}

		//Unmarshal the error response into a string
		errStr := string(body)
		return nil, errors.New(errStr)
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
			kSplit := strings.Split(k, ":")
			switch kSplit[0] {
			case mountPoints:
				mountPointsMap[kSplit[1]] = v.Value
			case portAvailable:
				portsMap[kSplit[1]] = v.Value
			default:
				//Try Getting Python Version.
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

	//Marshal the mount points in the request.
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
