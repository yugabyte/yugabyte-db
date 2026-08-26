// Copyright (c) YugabyteDB, Inc.

package main

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"node-agent/ynp/command"
	"node-agent/ynp/config"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/spf13/cobra"
)

const (
	// YNP cloud config passed down from YBA.
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

	// YNP onprem manual config.
	ynpOnpremConfigYamlContent = `
ynp:
  chrony_servers: []
  yb_home_dir: /home/yugabyte
  yb_user_home: /home/yugabyte
  yb_user_id: 1004
  no_proxy_list: []
  is_airgap: false
  node_ip: 127.0.0.1
  tmp_directory: /tmp
  node_agent_port: 9070
  node_exporter_port: 9300
  is_configure_clockbound: false
  configure_cgroup: true
yba:
  url: https://yba.example.com
  skip_tls_verify: true
  customer_uuid: 11111111-1111-1111-1111-111111111111
  api_key: test-api-key
  node_name: node-1
  node_external_fqdn: 10.0.0.1
  provider:
    name: onprem-provider-1
    region:
      name: us-west-1
      zone:
        name: us-west-1a
      latitude: 37.77
      longitude: -122.42
  instance_type:
    name: c5.large
    cores: 4
    memory_size: 16
    volume_size: 100
    mount_points: ["/mnt/d0"]
logging:
  level: INFO
  directory: ./logs
  file: app.log
`
)

func yamlConfigFilePath(t *testing.T, content string) string {
	tmpFile, err := os.CreateTemp("/tmp", "ynp_config_*.yaml")
	if err != nil {
		t.Fatalf("Failed to create temp YAML config: %v", err)
	}
	defer tmpFile.Close()
	if _, err := tmpFile.WriteString(content); err != nil {
		t.Fatalf("Failed to write temp YAML config: %v", err)
	}
	return tmpFile.Name()
}

func testRootCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "node-agent-provision ...",
		Short: "Command for node agent provisioner",
		RunE: func(cmd *cobra.Command, args []string) error {
			return handleCommand(cmd, args, map[string]config.CommandFactory{
				"provision": NewTestProvisionCommand,
			})
		},
	}
}

func getScriptPath(t *testing.T, tmpPath string, suffix string) string {
	entries, err := os.ReadDir(tmpPath)
	if err != nil {
		t.Fatalf("Failed to read directory: %v", err)
	}
	for _, entry := range entries {
		if !entry.IsDir() && strings.HasSuffix(entry.Name(), suffix) {
			return filepath.Join(tmpPath, entry.Name())
		}
	}
	t.Fatalf("No script found with suffix %s", suffix)
	return ""
}

// Check for the presence of the overridden config values in the generated config.ini file.
func checkLine(t *testing.T, iniFile string, searchStrings []string) {
	file, err := os.Open(iniFile)
	if err != nil {
		t.Fatalf("unable to open file: %v", err)
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)
	unprocessed := map[string]struct{}{}
	for _, str := range searchStrings {
		unprocessed[str] = struct{}{}
	}
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		for _, searchString := range searchStrings {
			if line == searchString {
				t.Logf("Found %s", searchString)
				delete(unprocessed, searchString)
			}
		}
	}
	for str := range unprocessed {
		t.Logf("Search string %s is not found", str)
	}
	if len(unprocessed) > 0 {
		t.Fatalf("Not all search strings are found in the file")
	}
}

func configFilePath(t *testing.T, content string) string {
	configPath := filepath.Join(os.TempDir(), "config_*.json")
	err := os.WriteFile(configPath, []byte(content), 0644)
	if err != nil {
		t.Fatalf("Failed to write config file: %v", err)
	}
	return configPath
}

// Returns the destination opened file handle.
// Caller is responsible for closing the file handle.
func copyIniFiletoTemp(t *testing.T, srcPath string) *os.File {
	srcFile, err := os.Open(srcPath)
	if err != nil {
		t.Fatalf("Failed to open source file: %v", err)
	}
	defer srcFile.Close()
	tmpFile, err := os.CreateTemp("/tmp", "ynp_config_*.ini")
	if err != nil {
		t.Fatalf("Failed to create temporary file: %v", err)
	}
	_, err = io.Copy(tmpFile, srcFile)
	if err != nil {
		os.Remove(tmpFile.Name())
		tmpFile.Close()
		t.Fatalf("Failed to copy file: %v", err)
	}
	t.Logf("Written temp INI file at %s\n", tmpFile.Name())
	return tmpFile
}

type TestProvisionCommand struct {
	*command.ProvisionCommand
}

func NewTestProvisionCommand(ctx context.Context,
	iniConfig *config.INIConfig,
	args *config.Args,
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

// TestYNPProvision tests the basic generation of the scripts.
func TestYNPProvision(t *testing.T) {
	configPath := configFilePath(t, ynpCloudConfig)
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")
	yamlConfigPath := filepath.Join(projectDir, "resources/node-agent-provision.yaml")
	testRootCmd := testRootCmd()
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

// TestNoDuplicateConfig tests that if there are duplicate keys in the config in the same section.
func TestNoDuplicateConfig(t *testing.T) {
	configMap := map[string]map[string]any{}
	err := json.Unmarshal([]byte(ynpCloudConfig), &configMap)
	if err != nil {
		t.Fatalf("\nError unmarshaling ynp config %v\n", err)
	}
	// loglevel is already present in config.j2 in DEFAULT section.
	configMap["extra"]["loglevel"] = "DEBUG"
	ba, err := json.Marshal(configMap)
	if err != nil {
		t.Fatalf("\nError marshaling ynp config %v\n", err)
	}
	configPath := filepath.Join(os.TempDir(), "config.json")
	err = os.WriteFile(configPath, ba, 0644)
	if err != nil {
		t.Fatalf("Failed to write config file: %v", err)
	}
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")
	yamlConfigPath := filepath.Join(projectDir, "resources/node-agent-provision.yaml")
	testRootCmd := testRootCmd()
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
	if err := testRootCmd.Execute(); err == nil {
		t.Fatalf("Expected to fail")
	} else if !strings.Contains(err.Error(), "Duplicate key loglevel in section DEFAULT") {
		t.Fatalf("Expected error about duplicate key, got %v", err)
	}
}

// TestSchemaValidationOnPremManual verifies that when extra_vars is not set,
// the YAML config is validated against the schema (onprem manual case).
func TestSchemaValidationOnPremManual(t *testing.T) {
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")

	mountPointsAsString := strings.Replace(
		ynpOnpremConfigYamlContent,
		`mount_points: ["/mnt/d0"]`,
		`mount_points: "/mnt/d0"`,
		1,
	)
	mountPointsAsStringPath := yamlConfigFilePath(t, mountPointsAsString)
	defer os.Remove(mountPointsAsStringPath)

	cases := []struct {
		name       string
		configPath string
	}{
		{
			name:       "placeholder yaml",
			configPath: filepath.Join(projectDir, "resources/node-agent-provision.yaml"),
		},
		{
			name:       "mount_points as string", /* Invalid case */
			configPath: mountPointsAsStringPath,
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			testRootCmd := testRootCmd()
			testRootCmd.SetArgs([]string{
				"--ynp_base_path",
				ynpBasePath,
				"--config_file",
				tc.configPath,
				"--dry_run",
				"--skip_module",
				"InstallNodeAgent", /* This needs YBA */
				"--skip_module",
				"ConfigureSystemd", /* This requires some setup */
			})
			setupCommand(testRootCmd)
			if err := testRootCmd.Execute(); err == nil {
				t.Fatalf("Expected schema validation to fail when extra_vars is not set")
			} else if !strings.Contains(err.Error(), "Failed to validate config") {
				t.Fatalf("Expected schema validation error, got %v", err)
			}
		})
	}
}

// TestConfigOverride tests that the config overrides are applied correctly and reflected
// in the generated config.
func TestConfigOverride(t *testing.T) {
	configPath := configFilePath(t, ynpCloudConfig)
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")
	yamlConfigPath := filepath.Join(projectDir, "resources/node-agent-provision.yaml")
	testRootCmd := testRootCmd()
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
		"--config_override",
		`ynp.node_ip="10.20.30.40"`,
		"--config_override",
		`ynp.chrony_servers=["1.2.3.4", "5.6.7.8"]`,
		"--config_override",
		`yba.instance_type.name="large"`,
	})
	setupCommand(testRootCmd)
	if err := testRootCmd.Execute(); err != nil {
		t.Fatalf("Error executing command: %v\n", err)
	}
	iniFile := filepath.Join(ynpBasePath, "configs/config.ini")
	checkLine(t, iniFile, []string{
		"bind_ip = 10.20.30.40",
		"chrony_servers = 1.2.3.4, 5.6.7.8",
		"instance_type_name = large",
	})
}

// TestNonExistingEnabledModule tests the scenario where a module is defined in INI but not registered in the command.
func TestNonExistingEnabledModule(t *testing.T) {
	configPath := configFilePath(t, ynpCloudConfig)
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")
	yamlConfigPath := filepath.Join(projectDir, "resources/node-agent-provision.yaml")
	configIniFile := filepath.Join(ynpBasePath, "configs/config.j2")
	tempConfigIniFile := copyIniFiletoTemp(t, configIniFile)
	defer os.Remove(tempConfigIniFile.Name())
	defer tempConfigIniFile.Close()
	tempConfigIniFile.WriteString("\n[NON_EXISTING]\n")
	tempConfigIniFile.Close()
	testRootCmd := testRootCmd()
	testRootCmd.SetArgs([]string{
		"--ynp_base_path",
		ynpBasePath,
		"--config_file",
		yamlConfigPath,
		"--config_ini",
		tempConfigIniFile.Name(),
		"--extra_vars",
		configPath,
		"--dry_run",
		"--skip_module",
		"InstallNodeAgent", /* This needs YBA */
		"--skip_module",
		"ConfigureSystemd", /* This requires some setup */
	})
	setupCommand(testRootCmd)
	if err := testRootCmd.Execute(); err == nil {
		t.Fatalf("Expected to fail")
	} else if !strings.Contains(err.Error(), "Module NON_EXISTING is defined in INI but not registered in the command") {
		t.Fatalf("Expected error about invalid template path, got %v", err)
	}
}

// TestEnabledModuleInvalidTemplatePath tests the scenario where a module is defined in INI but has invalid template path.
func TestEnabledModuleInvalidTemplatePath(t *testing.T) {
	configPath := configFilePath(t, ynpCloudConfig)
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")
	yamlConfigPath := filepath.Join(projectDir, "resources/node-agent-provision.yaml")
	configIniFile := filepath.Join(ynpBasePath, "configs/config.j2")
	tempConfigIniFile := copyIniFiletoTemp(t, configIniFile)
	defer os.Remove(tempConfigIniFile.Name())
	defer tempConfigIniFile.Close()
	tempConfigIniFile.WriteString("\n[ConfigureOtelcol]\n")
	tempConfigIniFile.Close()
	testRootCmd := testRootCmd()
	testRootCmd.SetArgs([]string{
		"--ynp_base_path",
		ynpBasePath,
		"--config_file",
		yamlConfigPath,
		"--config_ini",
		tempConfigIniFile.Name(),
		"--extra_vars",
		configPath,
		"--dry_run",
		"--skip_module",
		"InstallNodeAgent", /* This needs YBA */
		"--skip_module",
		"ConfigureSystemd", /* This requires some setup */
	})
	setupCommand(testRootCmd)
	if err := testRootCmd.Execute(); err == nil {
		t.Fatalf("Expected to fail")
	} else if !strings.Contains(err.Error(), "otelcol does not exist for module ConfigureOtelcol") {
		t.Fatalf("Expected error about invalid template path, got %v", err)
	}
}

func TestYNPProvisionWithYbHomeDir(t *testing.T) {
	configPath := configFilePath(t, ynpCloudConfig)
	projectDir := os.Getenv("PROJECT_DIR")
	ynpBasePath := filepath.Join(projectDir, "resources/ynp")
	yamlConfigPath := filepath.Join(projectDir, "resources/node-agent-provision.yaml")
	tmpDir, err := os.MkdirTemp("/tmp", "ynp-test-*")
	if err != nil {
		t.Fatalf("Failed to create temporary directory: %v", err)
	}
	defer os.RemoveAll(tmpDir)
	testRootCmd := testRootCmd()
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
		"--config_override",
		`ynp.yb_home_dir="/home/yugabyte"`,
		"--config_override",
		`ynp.yb_user_home="/home/software"`,
		"--config_override",
		`ynp.tmp_directory="` + tmpDir + `"`,
	})
	setupCommand(testRootCmd)
	if err := testRootCmd.Execute(); err != nil {
		t.Fatalf("Error executing command: %v\n", err)
	}
	t.Logf("Generated scripts in %s", tmpDir)
	iniFile := filepath.Join(ynpBasePath, "configs/config.ini")
	checkLine(t, iniFile, []string{
		"yb_home_dir = /home/yugabyte",
		"yb_user_home = /home/software",
	})
	// Verify the precheck script is present.
	getScriptPath(t, tmpDir, "_precheck")
	// Verify the precheck script is present.
	getScriptPath(t, tmpDir, "_run")
}
