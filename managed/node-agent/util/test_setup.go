//go:build testonly
// +build testonly

// Copyright (c) YugaByte, Inc.
package util

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"node-agent/model"
	"os"
	"strings"

	"github.com/gorilla/mux"
)

const (
	dummyPuuid        = "puuid"
	dummyInstanceType = "instance_type_0"
	dummyRegion       = "region_0"
	dummyZone         = "zone_0"
)

func GetTestConfig() (*Config, error) {
	//Sets the env to test to load test config.
	os.Setenv("env", "TEST")
	//Create test config
	err := InitConfig("config_test")
	if err != nil {
		fmt.Printf("Failed to load test config - %s", err.Error())
		return nil, err
	}
	Config := GetConfig()
	server := MockServer()
	serverUrl := strings.Split(server.URL, ":")
	Config.Update(PlatformHost, string(serverUrl[0])+":"+string(serverUrl[1]))
	Config.Update(PlatformPort, string(serverUrl[2]))
	Config.Update(PlatformVersion, "1")
	Config.Update(UserId, "u1234")
	Config.Update(ProviderId, "p1234")
	Config.Update(CustomerId, "c1234")
	Config.Update(NodeIP, "127.0.0.1")
	Config.Update(RequestTimeout, "100")
	Config.Update(NodeName, "nodeName")
	Config.Update(NodeAgentId, "n1234")
	Config.Update(NodeRegion, dummyRegion)
	Config.Update(NodeZone, dummyZone)
	Config.Update(NodeAzId, "az1234")
	Config.Update(NodeInstanceType, dummyInstanceType)
	Config.Update(NodeLogger, "node_agent_test.log")

	InitCommonLoggers()
	return Config, nil
}

//Sets up a mock server to test http client calls.
func MockServer() *httptest.Server {
	r := mux.NewRouter()

	//Handle different routes for testing.
	r.HandleFunc("/api/v1/customers/{cuuid}/node_agents", registerNodeTestHandler)
	r.HandleFunc("/api/v1/customers/{cuuid}/node_agents/{nuuid}", nodeTestHandler)
	r.HandleFunc(
		"/api/customers/{cuuid}/providers/{puuid}/instance_types/{instanceType}",
		getInstanceTypeTestHandler,
	)
	r.HandleFunc("/test", testHandler)
	r.HandleFunc("/api/customers/{cuuid}/zones/{azid}/nodes", nodeCapabilitiesTestHandler)
	r.HandleFunc("/customers/{cuuid}/node_agents/{nuuid}/state", nodeAgentStateHandler)

	return httptest.NewServer(r)
}

//Todo: Create a mock request handler for state updates requests.
func nodeAgentStateHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodPut {
		//Todo
	} else {
		w.Write([]byte("success"))
	}
}

func getInstanceTypeTestHandler(w http.ResponseWriter, r *http.Request) {
	//vars := mux.Vars(r)
	data, err := json.Marshal(GetTestInstanceTypeData())
	if err != nil {
		http.Error(w, "Internal Server Error", 500)
		return
	}
	w.Write(data)
}

func testHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodGet {
		w.Write([]byte("success"))
	}

	if r.Method == http.MethodPut {
		http.Error(w, "Invalid request Method", 405)
	}
}
func registerNodeTestHandler(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	if vars["cuuid"] != "c1234" {
		http.Error(w, "{\"success\": false, \"error\": \"Bad Request\"}", 400)
		return
	}
	data, err := json.Marshal(GetTestRegisterResponse())
	if err != nil {
		http.Error(w, "Internal Server Error", 500)
		return
	}
	w.Write(data)
}

func nodeTestHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodDelete {
		http.Error(w, "Invalid request method.", 405)
		return
	}
	if r.Method == http.MethodDelete {
		res := model.RegisterResponseEmpty{}
		res.SuccessStatus = true
		res.Message = "Deleted node"
		data, err := json.Marshal(res)
		if err != nil {
			panic(err)
		}
		w.Write([]byte(data))
		return
	}
	w.Write([]byte("success"))
}

func nodeCapabilitiesTestHandler(w http.ResponseWriter, r *http.Request) {
	response := fmt.Sprintf(
		"{\"127.0.0.1\":{\"region\":\"%s\", \"instanceType\":\"%s\", \"zone\":\"%s\"}}",
		dummyRegion,
		dummyInstanceType,
		dummyZone,
	)
	w.Write([]byte(response))
}

func GetTestRegisterResponse() model.RegisterResponseSuccess {
	commonInfo := model.CommonInfo{
		Name:    "nodeName",
		IP:      "127.0.0.1",
		Version: "1",
	}
	config := model.NodeAgentConfig{
		ServerCert: "test_server_cert",
		ServerKey:  "test_server_key",
	}
	response := model.RegisterResponseSuccess{
		NodeAgent: model.NodeAgent{
			CommonInfo:   commonInfo,
			Uuid:         "n1234",
			UpdatedAt:    1234,
			Config:       config,
			CustomerUuid: "c1234",
		},
	}
	return response
}

func GetTestInstanceTypeData() model.NodeInstanceType {
	volumeDetails := model.VolumeDetails{VolumeSize: 100, MountPath: "/home"}
	nodeInstanceDetails := model.NodeInstanceTypeDetails{
		VolumeDetailsList: []model.VolumeDetails{volumeDetails},
	}
	dummyProvider := model.Provider{
		SshPort: 54422,
	}
	dummyProvider.BasicInfo.Uuid = "p1234"
	result := model.NodeInstanceType{
		Active:           false,
		NumCores:         10,
		MemSizeGB:        10,
		Details:          nodeInstanceDetails,
		InstanceTypeCode: "instance_type_0",
		Provider:         dummyProvider,
	}
	return result
}
