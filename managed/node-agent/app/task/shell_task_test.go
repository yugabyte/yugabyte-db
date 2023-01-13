// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"fmt"
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

	if result != "test\n" {
		t.Fatalf("Unexpected result")
	}
}

func TestGetOptions(t *testing.T) {
	providerData := util.GetTestProviderData()
	dummyInstanceType := util.GetTestInstanceTypeData()

	flagTests := []struct {
		provider     model.Provider
		accessKey    model.AccessKey
		instanceType model.NodeInstanceType
		expected     []string
	}{
		{
			providerData,
			util.GetTestAccessKeyData(true, false, false),
			dummyInstanceType,
			[]string{
				"./dummy.sh",
				"-t",
				"provision",
				"--yb_home_dir",
				"'/home/yugabyte/custom'",
				"--ssh_port",
				"54422",
				"--mount_points",
				"/home",
				"--install_node_exporter",
			},
		},
		{
			providerData,
			util.GetTestAccessKeyData(false, true, false),
			dummyInstanceType,
			[]string{
				"./dummy.sh",
				"-t",
				"configure",
				"--yb_home_dir",
				"'/home/yugabyte/custom'",
				"--ssh_port",
				"54422",
				"--mount_points",
				"/home",
			},
		},
		{
			providerData,
			util.GetTestAccessKeyData(true, false, true),
			dummyInstanceType,
			[]string{
				"./dummy.sh",
				"-t",
				"provision",
				"--yb_home_dir",
				"'/home/yugabyte/custom'",
				"--ssh_port",
				"54422",
				"--mount_points",
				"/home",
				"--install_node_exporter",
				"--airgap",
			},
		},
	}

	for _, tt := range flagTests {
		testName := fmt.Sprintf(
			"InstallNodeExporter: %t, SkipProvisioning: %t, AirGapInstall: %t",
			tt.accessKey.KeyInfo.InstallNodeExporter,
			tt.accessKey.KeyInfo.SkipProvisioning,
			tt.accessKey.KeyInfo.AirGapInstall,
		)
		t.Run(testName, func(t *testing.T) {
			handler := NewPreflightCheckHandler(&tt.provider, &tt.instanceType, &tt.accessKey)
			result := handler.getOptions("./dummy.sh")
			isEqual := reflect.DeepEqual(result, tt.expected)
			if !isEqual {
				t.Fatalf("Incorrect preflight run options.")
			}
		})
	}
}
