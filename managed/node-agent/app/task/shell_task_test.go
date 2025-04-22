// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"fmt"
	pb "node-agent/generated/service"
	"node-agent/model"
	"node-agent/util"
	"reflect"
	"testing"
)

func TestShellTaskProcess(t *testing.T) {
	testShellTask := NewShellTask("test_echo", "echo", []string{"test"})
	ctx := context.Background()
	result, err := testShellTask.Process(ctx)
	if err != nil {
		t.Fatalf("Error while running shell task - %s", err.Error())
	}

	if result.ExitStatus.Code != 0 {
		t.Fatalf("Error while running shell task - %d", result.ExitStatus.Code)
	}

	if result.Info.String() != "test\n" {
		t.Fatalf("Unexpected result")
	}
}

func TestGetOptions(t *testing.T) {
	flagTests := []struct {
		param    *model.PreflightCheckParam
		expected []string
	}{
		{
			&model.PreflightCheckParam{
				SkipProvisioning:     false,
				AirGapInstall:        true,
				InstallNodeExporter:  true,
				YbHomeDir:            "/home/yugabyte",
				SshPort:              22,
				MountPaths:           []string{"/tmp1", "/tmp2"},
				MasterHttpPort:       1,
				MasterRpcPort:        2,
				TserverHttpPort:      3,
				TserverRpcPort:       4,
				RedisServerHttpPort:  5,
				RedisServerRpcPort:   6,
				NodeExporterPort:     7,
				YcqlServerHttpPort:   8,
				YcqlServerRpcPort:    9,
				YsqlServerHttpPort:   10,
				YsqlServerRpcPort:    11,
				YbControllerHttpPort: 12,
				YbControllerRpcPort:  13,
			},
			[]string{
				"./dummy.sh",
				"-t",
				"provision",
				"--node_agent_mode",
				"--yb_home_dir",
				"'/home/yugabyte'",
				"--ssh_port",
				"22",
				"--master_http_port",
				"1",
				"--master_rpc_port",
				"2",
				"--tserver_http_port",
				"3",
				"--tserver_rpc_port",
				"4",
				"--redis_server_http_port",
				"5",
				"--redis_server_rpc_port",
				"6",
				"--node_exporter_port",
				"7",
				"--ycql_server_http_port",
				"8",
				"--ycql_server_rpc_port",
				"9",
				"--ysql_server_http_port",
				"10",
				"--ysql_server_rpc_port",
				"11",
				"--yb_controller_http_port",
				"12",
				"--yb_controller_rpc_port",
				"13",
				"--mount_points",
				"/tmp1,/tmp2",
				"--install_node_exporter",
				"--airgap",
			},
		},
		{
			&model.PreflightCheckParam{
				SkipProvisioning:     true,
				AirGapInstall:        true,
				InstallNodeExporter:  true,
				YbHomeDir:            "/home/yugabyte",
				SshPort:              522,
				MountPaths:           []string{"/tmp1", "/tmp2"},
				MasterHttpPort:       1,
				MasterRpcPort:        2,
				TserverHttpPort:      3,
				TserverRpcPort:       4,
				RedisServerHttpPort:  5,
				RedisServerRpcPort:   6,
				NodeExporterPort:     7,
				YcqlServerHttpPort:   8,
				YcqlServerRpcPort:    9,
				YsqlServerHttpPort:   10,
				YsqlServerRpcPort:    11,
				YbControllerHttpPort: 12,
				YbControllerRpcPort:  13,
			},
			[]string{
				"./dummy.sh",
				"-t",
				"configure",
				"--node_agent_mode",
				"--yb_home_dir",
				"'/home/yugabyte'",
				"--ssh_port",
				"522",
				"--master_http_port",
				"1",
				"--master_rpc_port",
				"2",
				"--tserver_http_port",
				"3",
				"--tserver_rpc_port",
				"4",
				"--redis_server_http_port",
				"5",
				"--redis_server_rpc_port",
				"6",
				"--node_exporter_port",
				"7",
				"--ycql_server_http_port",
				"8",
				"--ycql_server_rpc_port",
				"9",
				"--ysql_server_http_port",
				"10",
				"--ysql_server_rpc_port",
				"11",
				"--yb_controller_http_port",
				"12",
				"--yb_controller_rpc_port",
				"13",
				"--mount_points",
				"/tmp1,/tmp2",
				"--install_node_exporter",
				"--airgap",
			},
		},
		{
			&model.PreflightCheckParam{
				SkipProvisioning:    false,
				AirGapInstall:       true,
				InstallNodeExporter: true,
				YbHomeDir:           "/home/yugabyte",
				SshPort:             522,
				MountPaths:          []string{"/tmp1", "/tmp2"},
				MasterHttpPort:      1,
				MasterRpcPort:       2,
				TserverHttpPort:     3,
				TserverRpcPort:      4,
				RedisServerHttpPort: 5,
			},
			[]string{
				"./dummy.sh",
				"-t",
				"provision",
				"--node_agent_mode",
				"--yb_home_dir",
				"'/home/yugabyte'",
				"--ssh_port",
				"522",
				"--master_http_port",
				"1",
				"--master_rpc_port",
				"2",
				"--tserver_http_port",
				"3",
				"--tserver_rpc_port",
				"4",
				"--redis_server_http_port",
				"5",
				"--mount_points",
				"/tmp1,/tmp2",
				"--install_node_exporter",
				"--airgap",
			},
		},
	}

	for i, tt := range flagTests {
		testName := fmt.Sprintf("TestCase-%d", i)
		t.Run(testName, func(t *testing.T) {
			handler := NewPreflightCheckHandler(tt.param)
			result := handler.getOptions("./dummy.sh")
			isEqual := reflect.DeepEqual(result, tt.expected)
			if !isEqual {
				t.Fatalf(
					"Incorrect preflight run options in(%s) - expected %v, found %v",
					testName,
					tt.expected,
					result,
				)
			}
		})
	}
}

func TestPreflightCheckParamConversion(t *testing.T) {
	gInput := pb.PreflightCheckInput{
		SkipProvisioning:     true,
		AirGapInstall:        true,
		InstallNodeExporter:  true,
		YbHomeDir:            "/home/yugabyte",
		SshPort:              522,
		MountPaths:           []string{"/tmp1", "/tmp2"},
		MasterHttpPort:       1,
		MasterRpcPort:        2,
		TserverHttpPort:      3,
		TserverRpcPort:       4,
		RedisServerHttpPort:  5,
		RedisServerRpcPort:   6,
		NodeExporterPort:     7,
		YcqlServerHttpPort:   8,
		YcqlServerRpcPort:    9,
		YsqlServerHttpPort:   10,
		YsqlServerRpcPort:    11,
		YbControllerHttpPort: 12,
		YbControllerRpcPort:  13,
	}
	param := model.PreflightCheckParam{}
	err := util.ConvertType(&gInput, &param)
	if err != nil {
		t.Fatal(err)
	}
	gValue := reflect.ValueOf(gInput)
	pValue := reflect.ValueOf(param)
	pType := pValue.Type()
	for i := 0; i < pType.NumField(); i++ {
		sField := pType.Field(i)
		// Convert to string as type comparison like uint32 and int can fail.
		pField := fmt.Sprintf("%v", pValue.FieldByName(sField.Name).Interface())
		gField := fmt.Sprintf("%v", gValue.FieldByName(sField.Name).Interface())
		if pField != gField {
			t.Fatalf("Expected %v, found %v", gField, pField)
		}
	}
}
