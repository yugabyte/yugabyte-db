// Copyright (c) YugabyteDB, Inc.

package main

import (
	"context"
	"node-agent/ynp/command"
	"node-agent/ynp/config"
	"os"
	"path/filepath"
	"testing"

	"github.com/spf13/cobra"
)

const (
	ynpCloudConfig = `
{
  "ynp" : {
    "node_ip" : "10.9.118.237",
    "is_install_node_agent" : false,
    "yb_user_id" : "1994",
    "is_airgap" : false,
    "is_yb_prebuilt_image" : false,
    "is_ybcontroller_disabled" : false,
    "node_exporter_port" : "9310",
    "tmp_directory" : "/tmp",
    "is_configure_clockbound" : false,
    "yb_home_dir" : "/home/yugabyte",
    "earlyoom" : {
      "earlyoom_enable" : true,
      "earlyoom_args" : ""
    }
  },
  "extra" : {
    "cloud_type" : "aws",
    "is_cloud" : true,
    "mount_paths" : "/mnt/d0",
    "package_path" : "/opt/yugabyte/node-agent/thirdparty",
    "device_paths" : "xvdb"
  },
  "logging" : {
    "level" : "DEBUG",
    "directory" : "/tmp/ynp_logs"
  }
}
`
)

var (
	testRootCmd = &cobra.Command{
		Use:   "node-agent-provision ...",
		Short: "Command for node agent provisioner",
		Run: func(cmd *cobra.Command, args []string) {
			handleCommand(cmd, args, map[string]config.CommandFactory{
				"provision": NewTestProvisionCommand,
			})
		},
	}
)

type TestProvisionCommand struct {
	*command.ProvisionCommand
}

func NewTestProvisionCommand(ctx context.Context,
	iniConfig *config.INIConfig,
	args config.Args,
) config.Command {
	provisionCmd := command.NewProvisionCommand(ctx, iniConfig, args)
	return &TestProvisionCommand{
		ProvisionCommand: provisionCmd.(*command.ProvisionCommand),
	}
}

func (c *TestProvisionCommand) Init() error {
	c.SetOSInfo(command.RedHat, "rhel", "9", command.RPM)
	c.RegisterModules()
	return nil
}

// TestYNP tests the basic generation of the scripts.
func TestYNP(t *testing.T) {
	configPath := filepath.Join(os.TempDir(), "config.json")
	err := os.WriteFile(configPath, []byte(ynpCloudConfig), 0644)
	if err != nil {
		t.Fatalf("Failed to write config file: %v", err)
	}
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")
	yamlConfigPath := filepath.Join(projectDir, "resources/node-agent-provision.yaml")
	testRootCmd.SetArgs([]string{
		"--ynp_base_path",
		ynpBasePath,
		"--config_file",
		yamlConfigPath,
		"--extra_vars",
		configPath,
		"--dry_run",
		"--skip_module",
		"InstallNodeAgent", /* This needs YBA */
		"--skip_module",
		"ConfigureSystemd", /* This requires some setup */
	})
	setupCommand(testRootCmd)
	if err := testRootCmd.Execute(); err != nil {
		t.Fatalf("Error executing command: %v\n", err)
	}
}
