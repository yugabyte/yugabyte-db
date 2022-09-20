// Copyright (c) YugaByte, Inc.

package task

import (
	"bytes"
	"context"
	"io/ioutil"
	"net/http"
	"node-agent/model"
	"node-agent/util"
	"testing"
)

var dummyApiToken = "123456"

func TestHandleAgentRegistration(t *testing.T) {
	config := util.CurrentConfig()
	t.Logf("cuuid: %s", config.String(util.CustomerIdKey))
	testRegistrationHandler := NewAgentRegistrationHandler(dummyApiToken)
	ctx := context.Background()
	result, err := testRegistrationHandler.Handle(ctx)

	if err != nil {
		t.Errorf("Error while running test registration handler - %s", err.Error())
		return
	}

	//Test Success Response
	data, ok := result.(*model.RegisterResponseSuccess)
	if !ok {
		t.Errorf("Error while inferencing data to Register response success")
		return
	}

	if data.CustomerUuid != "c1234" {
		t.Errorf("Error in the response data.")
	}
}

func TestHandleAgentRegistrationFailure(t *testing.T) {
	config := util.CurrentConfig()
	cuid := config.String(util.CustomerIdKey)
	config.Update(util.CustomerIdKey, "dummy")
	defer config.Update(util.CustomerIdKey, cuid)
	testRegistrationHandler := NewAgentRegistrationHandler(dummyApiToken)
	ctx := context.Background()
	_, err := testRegistrationHandler.Handle(ctx)

	if err == nil {
		t.Errorf("Expected error")
		return
	}

	if err.Error() != "Bad Request" {
		t.Errorf("Expected a different error")
	}
}

func TestHandleAgentUnregistration(t *testing.T) {
	testUnregistrationHandler := NewAgentUnregistrationHandler(true, dummyApiToken)
	ctx := context.Background()
	result, err := testUnregistrationHandler.Handle(ctx)

	if err != nil {
		t.Errorf("Error while running test registration handler - %s", err.Error())
		return
	}

	//Test Success Response.
	data, ok := result.(*model.RegisterResponseEmpty)
	if !ok {
		t.Errorf("Error while inferencing data to Register response success")
		return
	}

	if data.SuccessStatus != true {
		t.Errorf("Error in the response data.")
	}
}

func TestUnmarshalResponse(t *testing.T) {
	res := http.Response{
		Body:       ioutil.NopCloser(bytes.NewBufferString("{\"test\":\"success\"}")),
		StatusCode: 200,
	}
	var testValue map[string]string
	data, err := UnmarshalResponse(&testValue, &res)
	if err != nil {
		t.Errorf("Unmarshaling error.")
		return
	}

	dataVal, ok := data.(*map[string]string)

	if !ok {
		t.Errorf("Unmarshaling inference error.")
		return
	}

	if (*dataVal)["test"] != "success" {
		t.Errorf("Unmarshaling assertion error.")
	}
}

func TestGetNodeConfig(t *testing.T) {
	data := getTestPreflightCheckVal()

	testNodeConfigList := getNodeConfig(data)

	mp, po, hds := false, false, false
	for _, v := range testNodeConfigList {
		switch v.Type {
		case "PORT_AVAILABLE":
			po = true
		case "HOME_DIR_SPACE":
			hds = true
		case "MOUNT_POINT":
			mp = true

		}
	}
	if !(mp && po && hds) {
		t.Errorf("Did not receive all the expected keys")
	}

}

func TestHandleGetPlatformConfig(t *testing.T) {
	handler := NewGetPlatformCurrentConfigHandler()
	response, err := handler.Handle(context.Background())
	if err != nil {
		t.Errorf("Unexpected error %s", err.Error())
	}

	if _, ok := response.(*model.NodeInstanceType); !ok {
		t.Errorf("Unexpected Type Inference Error")
	}
}

func TestHandleNodeCapability(t *testing.T) {
	data := getTestPreflightCheckVal()
	handler := NewSendNodeCapabilityHandler(data)
	testResponseData, err := handler.Handle(context.Background())
	if err != nil {
		t.Errorf("Unexpected Error %s ", err.Error())
	}

	testResponseDataMap, ok := testResponseData.(*map[string]model.NodeCapabilityResponse)
	if !ok {
		t.Errorf("Unexpected Type Inference Error")
	}

	if _, ok := (*testResponseDataMap)["127.0.0.1"]; !ok {
		t.Errorf("Unexpected Type Inference Error")
	}
}

func getTestPreflightCheckVal() map[string]model.PreflightCheckVal {
	data := make(map[string]model.PreflightCheckVal)
	data["port_available:1"] = model.PreflightCheckVal{Value: "false", Error: "none"}
	data["port_available:2"] = model.PreflightCheckVal{Value: "true", Error: "none"}
	data["port_available:3"] = model.PreflightCheckVal{Value: "none", Error: "test error"}
	data["home_dir_space"] = model.PreflightCheckVal{Value: "100", Error: "none"}
	data["mount_point:/opt"] = model.PreflightCheckVal{Value: "true", Error: "none"}
	data["mount_point:/tmp"] = model.PreflightCheckVal{Value: "true", Error: "none"}
	return data
}
