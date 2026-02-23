// Copyright (c) YugabyteDB, Inc.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"node-agent/util"
	"node-agent/ynp"
	"node-agent/ynp/command"
	"node-agent/ynp/config"
	"os"
	"path/filepath"

	"github.com/spf13/cobra"
	"gopkg.in/yaml.v2"
)

var (
	rootCmd = &cobra.Command{
		Use:   "node-agent-provision ...",
		Short: "Command for node agent provisioner",
		Run: func(cmd *cobra.Command, args []string) {
			handleCommand(cmd, args, map[string]config.CommandFactory{
				"provision": command.NewProvisionCommand,
			})
		},
	}
)

func setupCommand(cmd *cobra.Command) {
	cmd.Flags().String("command", "provision", "Command to execute")
	cmd.Flags().
		String("ynp_base_path", "./modules", "Path to the YNP module base directory")
	cmd.Flags().StringSlice("specific_module", []string{}, "Specific modules to execute")
	cmd.Flags().StringSlice("skip_module", []string{}, "Modules to skip from execution")
	cmd.Flags().String(
		"config_file",
		"./node-agent-provision.yaml",
		"Path to the ynp configuration file",
	)
	cmd.Flags().Bool(
		"preflight_check",
		false,
		"Execute the pre-flight check on the node",
	)
	cmd.Flags().Bool(
		"list_modules",
		false,
		"List all the registered modules",
	)
	cmd.Flags().String(
		"extra_vars",
		"{}",
		"Path to the JSON file or JSON string containing extra variables required for execution.",
	)
	cmd.Flags().Bool(
		"dry_run",
		false,
		"Render Execution Scripts without executing them for dry_run",
	)
	cmd.Flags().MarkHidden("ynp_base_path")
	cmd.MarkFlagRequired("ynp_base_path")
}

func parseArguments(cmd *cobra.Command) config.Args {
	command, err := cmd.Flags().GetString("command")
	if err != nil {
		log.Fatalf("Error parsing command flag: %v\n", err)
	}
	ynpBasePath, err := cmd.Flags().GetString("ynp_base_path")
	if err != nil {
		log.Fatalf("Error parsing ynp_base_path flag: %v\n", err)
	}
	specificModules, err := cmd.Flags().GetStringSlice("specific_module")
	if err != nil {
		log.Fatalf("Error parsing specific_module flag: %v\n", err)
	}
	skipModules, err := cmd.Flags().GetStringSlice("skip_module")
	if err != nil {
		log.Fatalf("Error parsing skip_module flag: %v\n", err)
	}
	configFile, err := cmd.Flags().GetString("config_file")
	if err != nil {
		log.Fatalf("Error parsing config_file flag: %v\n", err)
	}
	preflightCheck, err := cmd.Flags().GetBool("preflight_check")
	if err != nil {
		log.Fatalf("Error parsing preflight_check flag: %v\n", err)
	}
	listModules, err := cmd.Flags().GetBool("list_modules")
	if err != nil {
		log.Fatalf("Error parsing list_modules flag: %v\n", err)
	}
	extraVars, err := cmd.Flags().GetString("extra_vars")
	if err != nil {
		log.Fatalf("Error parsing extra_vars flag: %v\n", err)
	}
	dryRun, err := cmd.Flags().GetBool("dry_run")
	if err != nil {
		log.Fatalf("Error parsing dry_run flag: %v\n", err)
	}
	var exVars map[string]map[string]any
	if extraVars != "" {
		exVars, err = loadJSONOrFile(extraVars)
		if err != nil {
			log.Fatalf("Error loading extra_vars: %v\n", err)
		}
	}
	ynpConfig, err := loadYAMLConfig(configFile)
	if err != nil {
		log.Fatalf("Error loading YAML config: %v\n", err)
	}
	setDefaultConfigs(ynpConfig)
	mergeConfigs(ynpConfig, exVars)
	// Fix the types in the parsed config after merging the extra_vars.
	ynpConfig = config.FixParsedConfigMap(ynpConfig)
	return config.Args{
		Command:         command,
		YnpBasePath:     ynpBasePath,
		SpecificModules: specificModules,
		SkipModules:     skipModules,
		ConfigFile:      configFile,
		PreflightCheck:  preflightCheck,
		ListModules:     listModules,
		YnpConfig:       ynpConfig,
		DryRun:          dryRun,
	}
}

func loadJSONOrFile(jsonOrPath string) (map[string]map[string]any, error) {
	// Check if the input is a file path and if it exists.
	if _, err := os.Stat(jsonOrPath); err == nil {
		data, err := os.ReadFile(jsonOrPath)
		if err != nil {
			return nil, fmt.Errorf("Failed to read extra_vars file: %v", err)
		}
		var result map[string]map[string]any
		if err := json.Unmarshal(data, &result); err != nil {
			return nil, fmt.Errorf("Failed to parse extra_vars JSON file: %v", err)
		}
		return result, nil
	}
	// Try parsing as a JSON string.
	var result map[string]map[string]any
	if err := json.Unmarshal([]byte(jsonOrPath), &result); err != nil {
		return nil, fmt.Errorf("Invalid JSON or file path for extra_vars: %v", err)
	}
	return result, nil
}

func loadYAMLConfig(filePath string) (map[string]map[string]any, error) {
	absConfigPath, err := filepath.Abs(filePath)
	if err != nil {
		return nil, fmt.Errorf("Failed to get absolute path: %v", err)
	}
	configData, err := os.ReadFile(absConfigPath)
	if err != nil {
		return nil, fmt.Errorf("Parsing config file failed with: %v", err)
	}
	var ynpConfig map[string]map[string]any
	if err := yaml.Unmarshal(configData, &ynpConfig); err != nil {
		return nil, fmt.Errorf("Failed to parse YAML config: %v", err)
	}
	return ynpConfig, nil
}

// Set default configurations if not present for convenience.
// This ensures the basic values used in config.j2 are always set.
func setDefaultConfigs(ynpConfig map[string]map[string]any) {
	extraSection, ok := ynpConfig["extra"]
	if !ok {
		extraSection = make(map[string]any)
		ynpConfig["extra"] = extraSection
	}
	if _, ok := extraSection["cloud_type"]; !ok {
		extraSection["cloud_type"] = "onprem"
	}
	if _, ok := extraSection["is_cloud"]; !ok {
		extraSection["is_cloud"] = false
	}
	if _, ok := extraSection["is_ybm"]; !ok {
		extraSection["is_ybm"] = false
	}
}

// Merge extraVars into ynpConfig, giving preference to extraVars.
func mergeConfigs(
	ynpConfig map[string]map[string]any,
	extraVars map[string]map[string]any,
) {
	for key, value := range extraVars {
		if section, ok := ynpConfig[key]; ok {
			// Update existing section.
			for k, v := range value {
				section[k] = v
			}
		} else {
			// Add new section.
			ynpConfig[key] = value
		}
	}
}

func handleCommand(
	cmd *cobra.Command,
	args []string,
	commandFactories map[string]config.CommandFactory,
) {
	if len(args) > 0 {
		cmd.Help()
		log.Fatalf("Unknown non-flag args: %v", args)
	}
	ctx := context.Background()
	cmdArgs := parseArguments(cmd)
	// Setup logger first to use the custom logger.
	config.SetupLogger(ctx, cmdArgs.YnpConfig)
	iniConfig, err := config.GenerateConfigINI(ctx, cmdArgs)
	if err != nil {
		util.FileLogger().Fatalf(ctx, "Failed to generate config.ini: %v", err)
	}
	jsonConfig, _ := json.MarshalIndent(iniConfig.SectionValues(), "", "  ")
	util.ConsoleLogger().Debugf(ctx, "INI config here: %s", string(jsonConfig))
	executor := ynp.NewExecutor(iniConfig, cmdArgs)
	for name, factory := range commandFactories {
		executor.RegisterCommandFactory(name, factory)
	}
	err = executor.Exec(ctx)
	if err != nil {
		exitCode := 1
		var scriptErr *command.ScriptExitError
		if errors.As(err, &scriptErr) {
			exitCode = scriptErr.ExitCode
		}
		util.ConsoleLogger().Errorf(ctx, "Failed to execute provision command: %v", err)
		os.Exit(exitCode)
	}
}

func main() {
	setupCommand(rootCmd)
	if err := rootCmd.Execute(); err != nil {
		log.Fatalf("Error executing command: %v\n", err)
	}
}
