// Copyright (c) YugaByte, Inc.

package task

import (
	"bytes"
	"context"
	"io"
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
	testUnregistrationHandler := NewAgentUnregistrationHandler(dummyApiToken)
	ctx := context.Background()
	result, err := testUnregistrationHandler.Handle(ctx)

	if err != nil {
		t.Fatalf("Error while running test registration handler - %s", err.Error())
	}

	// Test Success Response.
	data, ok := result.(*model.ResponseMessage)
	if !ok {
		t.Fatalf("Error while inferencing data to Register response success")
	}

	if data.SuccessStatus != true {
		t.Fatalf("Error in the response data.")
	}
}

func TestUnmarshalResponse(t *testing.T) {
	res := http.Response{
		Body:       io.NopCloser(bytes.NewBufferString("{\"test\":\"success\"}")),
		StatusCode: 200,
	}
	var testValue map[string]string
	data, err := UnmarshalResponse(context.TODO(), &testValue, &res)
	if err != nil {
		t.Fatalf("Unmarshaling error.")
	}

	dataVal, ok := data.(*map[string]string)

	if !ok {
		t.Fatalf("Unmarshaling inference error.")
	}

	if (*dataVal)["test"] != "success" {
		t.Fatalf("Unmarshaling assertion error.")
	}
}

func TestGetNodeConfig(t *testing.T) {
	testNodeConfigList := getTestPreflightCheckOutput()
	mp, po, hds := false, false, false
	for _, v := range *testNodeConfigList {
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
		t.Fatalf("Did not receive all the expected keys")
	}

}

func TestHandleGetInstanceType(t *testing.T) {
	handler := NewGetInstanceTypeHandler()
	response, err := handler.Handle(context.Background())
	if err != nil {
		t.Fatalf("Unexpected error %s", err.Error())
	}

	if _, ok := response.(*model.NodeInstanceType); !ok {
		t.Fatalf("Unexpected Type Inference Error")
	}
}

func TestHandlePostNodeInstance(t *testing.T) {
	output := getTestPreflightCheckOutput()
	handler := NewPostNodeInstanceHandler(*output)
	testResponseData, err := handler.Handle(context.Background())
	if err != nil {
		t.Fatalf("Unexpected Error %s ", err.Error())
	}

	testResponseDataMap, ok := testResponseData.(*map[string]model.NodeInstanceResponse)
	if !ok {
		t.Fatalf("Unexpected Type Inference Error")
	}

	if _, ok := (*testResponseDataMap)["127.0.0.1"]; !ok {
		t.Fatalf("Unexpected Type Inference Error")
	}
}

func getTestPreflightCheckOutput() *[]model.NodeConfig {
	data := make(map[string]model.PreflightCheckVal)
	data["port_available:1"] = model.PreflightCheckVal{Value: "false"}
	data["port_available:2"] = model.PreflightCheckVal{Value: "true"}
	data["port_available:3"] = model.PreflightCheckVal{Value: "none"}
	data["home_dir_space"] = model.PreflightCheckVal{Value: "100"}
	data["mount_point:/opt"] = model.PreflightCheckVal{Value: "true"}
	data["mount_point:/tmp"] = model.PreflightCheckVal{Value: "true"}
	return getNodeConfig(data)
}
