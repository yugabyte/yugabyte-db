// Copyright (c) YugabyteDB, Inc.

package main

import (
	"context"
	"encoding/json"
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
		RunE: func(cmd *cobra.Command, args []string) error {
			// Propagate the error up to the main method for consistent error handling and testability.
			return handleCommand(cmd, args, map[string]config.CommandFactory{
				"provision": command.NewProvisionCommand,
			})
		},
	}
)

// Placeholder for parsed arguments.
type parsedArgs struct {
	config.Args
	// Additional local fields which are not passed down.
	extraVars string
}

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

func parseArguments(cmd *cobra.Command) (*parsedArgs, error) {
	command, err := cmd.Flags().GetString("command")
	if err != nil {
		return nil, fmt.Errorf("Error parsing command flag: %v\n", err)
	}
	ynpBasePath, err := cmd.Flags().GetString("ynp_base_path")
	if err != nil {
		return nil, fmt.Errorf("Error parsing ynp_base_path flag: %v\n", err)
	}
	specificModules, err := cmd.Flags().GetStringSlice("specific_module")
	if err != nil {
		return nil, fmt.Errorf("Error parsing specific_module flag: %v\n", err)
	}
	skipModules, err := cmd.Flags().GetStringSlice("skip_module")
	if err != nil {
		return nil, fmt.Errorf("Error parsing skip_module flag: %v\n", err)
	}
	configFile, err := cmd.Flags().GetString("config_file")
	if err != nil {
		return nil, fmt.Errorf("Error parsing config_file flag: %v\n", err)
	}
	preflightCheck, err := cmd.Flags().GetBool("preflight_check")
	if err != nil {
		return nil, fmt.Errorf("Error parsing preflight_check flag: %v\n", err)
	}
	listModules, err := cmd.Flags().GetBool("list_modules")
	if err != nil {
		return nil, fmt.Errorf("Error parsing list_modules flag: %v\n", err)
	}
	extraVars, err := cmd.Flags().GetString("extra_vars")
	if err != nil {
		return nil, fmt.Errorf("Error parsing extra_vars flag: %v\n", err)
	}
	dryRun, err := cmd.Flags().GetBool("dry_run")
	if err != nil {
		return nil, fmt.Errorf("Error parsing dry_run flag: %v\n", err)
	}
	return &parsedArgs{
		Args: config.Args{
			Command:         command,
			YnpBasePath:     ynpBasePath,
			SpecificModules: specificModules,
			SkipModules:     skipModules,
			ConfigFile:      configFile,
			PreflightCheck:  preflightCheck,
			ListModules:     listModules,
			DryRun:          dryRun,
		},
		extraVars: extraVars,
	}, nil
}

// Process the parsed arguments, read and merge the configuration, and setup the logger etc.
func processArguments(ctx context.Context, pArgs *parsedArgs) error {
	var err error
	var exVars map[string]map[string]any
	if pArgs.extraVars != "" {
		exVars, err = loadJSONOrFile(pArgs.extraVars)
		if err != nil {
			return fmt.Errorf("Error loading extra_vars: %v\n", err)
		}
	}
	ynpConfig, err := loadYAMLConfig(pArgs.ConfigFile)
	if err != nil {
		return fmt.Errorf("Error loading YAML config: %v\n", err)
	}
	setDefaultConfigs(ynpConfig)
	mergeConfigs(ynpConfig, exVars)
	// Setup logger first to use the custom logger.
	config.SetupLogger(ctx, pArgs.YnpConfig)
	// Fix the types in the parsed config after merging the extra_vars.
	ynpConfig = config.FixParsedConfigMap(ynpConfig)
	pArgs.YnpConfig = ynpConfig
	return nil
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
) error {
	if len(args) > 0 {
		cmd.Help()
		return fmt.Errorf("Unknown non-flag args: %v", args)
	}
	// Don't show error from here.
	cmd.SilenceErrors = true
	ctx := context.Background()
	// Parse the arguments into a structured format.
	pArgs, err := parseArguments(cmd)
	if err != nil {
		util.ConsoleLogger().Errorf(ctx, "Failed to parse the command arguments: %v", err)
		return err
	}
	// Don't show usage afterwards on error after the parsing is successful.
	cmd.SilenceUsage = true
	// Read and process the arguments.
	err = processArguments(ctx, pArgs)
	if err != nil {
		util.ConsoleLogger().Errorf(ctx, "Failed to process the command arguments: %v", err)
		return err
	}
	iniConfig, err := config.GenerateConfigINI(ctx, &pArgs.Args)
	if err != nil {
		util.ConsoleLogger().Errorf(ctx, "Failed to generate INI config: %v", err)
		return err
	}
	jsonConfig, _ := json.MarshalIndent(iniConfig.SectionValues(), "", "  ")
	util.ConsoleLogger().Debugf(ctx, "INI config here: %s", string(jsonConfig))
	executor := ynp.NewExecutor(iniConfig, &pArgs.Args)
	for name, factory := range commandFactories {
		executor.RegisterCommandFactory(name, factory)
	}
	err = executor.Exec(ctx)
	if err != nil {
		util.ConsoleLogger().Errorf(ctx, "Failed to execute provision command: %v", err)
		return err
	}
	return nil
}

func main() {
	setupCommand(rootCmd)
	if err := rootCmd.Execute(); err != nil {
		if !util.IsConsoleLoggerSetup() {
			// If logger was not set up, log to standard error as a fallback.
			log.Printf("Error executing command: %v\n", err)
		}
		os.Exit(1)
	}
}
