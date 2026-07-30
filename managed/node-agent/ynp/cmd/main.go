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
	"os/user"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
	"gopkg.in/yaml.v2"
)

const (
	defaultYnpBasePath        = "./modules"
	defaultConfigFile         = "./node-agent-provision.yaml"
	generatedConfigFileSuffix = "-generated.yaml"
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
	configOverrides []string
	extraVars       string
	generateConfig  bool
	generateAndRun  bool
}

func setupCommand(cmd *cobra.Command) {
	cmd.Flags().String("command", "provision", "Command to execute")
	cmd.Flags().
		String("ynp_base_path", defaultYnpBasePath, "Path to the YNP module base directory")
	cmd.Flags().StringArray("specific_module", []string{}, "Specific modules to execute")
	cmd.Flags().StringArray("skip_module", []string{}, "Modules to skip from execution")
	cmd.Flags().String(
		"config_file",
		defaultConfigFile,
		"Path to the ynp configuration file",
	)
	cmd.Flags().Bool(
		"preflight_check",
		false,
		"Execute the pre-flight check on the node",
	)
	cmd.Flags().String(
		"config_ini",
		"configs/config.j2",
		"Path to the INI configuration file",
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
	cmd.Flags().Bool(
		"noroot",
		false,
		"Run only modules that do not require root privileges",
	)
	cmd.Flags().Bool(
		"root",
		false,
		"Run only modules that require root privileges",
	)
	cmd.Flags().
		StringArray("config_override", []string{}, "Override config values in the format field1.field2.field3=value")
	cmd.Flags().Bool(
		"generate_config",
		false,
		"Generate YNP configuration file",
	)
	cmd.Flags().Bool(
		"generate_and_run",
		false,
		"Generate YNP configuration file and provision",
	)
	cmd.Flags().
		String("preflight_check_out_file", "", "Optional path to the preflight check output file")
	// Hide internally used flags from help output.
	cmd.Flags().MarkHidden("ynp_base_path")
	cmd.Flags().MarkHidden("extra_vars")
	cmd.Flags().MarkHidden("config_ini")
	cmd.MarkFlagsMutuallyExclusive("root", "noroot")
	cmd.MarkFlagsMutuallyExclusive("generate_config", "generate_and_run")
	cmd.MarkFlagRequired("ynp_base_path")
}

// Process the parsed arguments, read and merge the configuration, and setup the logger etc.
func parseArguments(cmd *cobra.Command) (*parsedArgs, error) {
	command, err := cmd.Flags().GetString("command")
	if err != nil {
		return nil, fmt.Errorf("Error parsing command flag: %v\n", err)
	}
	ynpBasePath, err := cmd.Flags().GetString("ynp_base_path")
	if err != nil {
		return nil, fmt.Errorf("Error parsing ynp_base_path flag: %v\n", err)
	}
	specificModules, err := cmd.Flags().GetStringArray("specific_module")
	if err != nil {
		return nil, fmt.Errorf("Error parsing specific_module flag: %v\n", err)
	}
	skipModules, err := cmd.Flags().GetStringArray("skip_module")
	if err != nil {
		return nil, fmt.Errorf("Error parsing skip_module flag: %v\n", err)
	}
	configFile, err := cmd.Flags().GetString("config_file")
	if err != nil {
		return nil, fmt.Errorf("Error parsing config_file flag: %v\n", err)
	}
	configIniFile, err := cmd.Flags().GetString("config_ini")
	if err != nil {
		return nil, fmt.Errorf("Error parsing config_ini flag: %v\n", err)
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
	noRoot, err := cmd.Flags().GetBool("noroot")
	if err != nil {
		return nil, fmt.Errorf("Error parsing noroot flag: %v\n", err)
	}
	root, err := cmd.Flags().GetBool("root")
	if err != nil {
		return nil, fmt.Errorf("Error parsing root flag: %v\n", err)
	}
	configOverrides, err := cmd.Flags().GetStringArray("config_override")
	if err != nil {
		return nil, fmt.Errorf("Error parsing config_override flag: %v\n", err)
	}
	generateConfig, err := cmd.Flags().GetBool("generate_config")
	if err != nil {
		return nil, fmt.Errorf("Error parsing generate_config flag: %v\n", err)
	}
	generateAndRun, err := cmd.Flags().GetBool("generate_and_run")
	if err != nil {
		return nil, fmt.Errorf("Error parsing generate_and_run flag: %v\n", err)
	}
	preflightCheckOutFile, err := cmd.Flags().GetString("preflight_check_out_file")
	if err != nil {
		return nil, fmt.Errorf("Error parsing preflight_check_out_file flag: %v\n", err)
	}
	return &parsedArgs{
		Args: config.Args{
			Command:               command,
			YnpBasePath:           ynpBasePath,
			SpecificModules:       specificModules,
			SkipModules:           skipModules,
			ConfigFile:            configFile,
			ConfigIniFile:         configIniFile,
			PreflightCheck:        preflightCheck,
			PreflightCheckOutFile: preflightCheckOutFile,
			ListModules:           listModules,
			DryRun:                dryRun,
			NoRoot:                noRoot,
			Root:                  root,
		},
		extraVars:       extraVars,
		configOverrides: configOverrides,
		generateConfig:  generateConfig,
		generateAndRun:  generateAndRun,
	}, nil
}

// Process the parsed arguments, read and merge the configuration, and setup the logger etc.
func processArguments(ctx context.Context, pArgs *parsedArgs) error {
	var err error
	var exVars map[string]map[string]any
	ynpConfig, err := loadYAMLConfig(pArgs.Args.ConfigFile)
	if err != nil {
		return fmt.Errorf("Error loading YAML config: %v", err)
	}
	// Initialize the schema handler to be used for validating config overrides and generating template YAML.
	schemaHandler, err := config.NewSchemaHandler(
		filepath.Join(pArgs.Args.YnpBasePath, config.YnpConfigSchemaPath),
	)
	if err != nil {
		return fmt.Errorf("Error in creating schema handler: %v", err)
	}
	if pArgs.extraVars == "" {
		// Onprem manual case. YAML should be fully populated.
		// TODO: Enable for CSPs once the types are fixed in Java code and AMI builders.
		err = schemaHandler.ValidateYnpConfig(ynpConfig)
		if err != nil {
			return err
		}
	} else {
		exVars, err = loadJSONOrFile(pArgs.extraVars)
		if err != nil {
			return fmt.Errorf("Error loading extra_vars: %v", err)
		}
	}
	if pArgs.PreflightCheckOutFile != "" {
		// Get the absolute path because the script can be executed from a different path.
		absPath, err := filepath.Abs(pArgs.PreflightCheckOutFile)
		if err != nil {
			return fmt.Errorf("Failed to get absolute path for preflight_check_out_file: %v", err)
		}
		pArgs.PreflightCheckOutFile = absPath
	}
	setDefaultConfigs(ynpConfig)
	mergeConfigs(ynpConfig, exVars)
	// Override config values from command line if any into the YNP config.
	err = schemaHandler.OverrideProperties(pArgs.configOverrides, ynpConfig)
	if err != nil {
		return fmt.Errorf("Error applying config overrides: %v", err)
	}
	// Fix the types in the parsed config after merging the extra_vars.
	pArgs.YnpConfig = config.FixParsedConfigMap(ynpConfig)
	// Setup logger now to use the custom logger with the final logging config.
	config.SetupLogger(ctx, pArgs.YnpConfig)
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
	return config.FixParsedConfigMap(result), nil
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
	return config.FixParsedConfigMap(ynpConfig), nil
}

func generateYnpConfigFileFromYba(ctx context.Context, cmdArgs *config.Args) (string, error) {
	// Initilize the YNP generator with the data provider that reads data from YBA.
	renderedConfig, err := config.NewYNPConfigGenerator(
		ctx,
		cmdArgs,
		config.NewDefaultResolverDataProvider(cmdArgs),
	).GenerateYnpConfig()
	if err != nil {
		return "", fmt.Errorf("Failed to generate YNP config: %v", err)
	}
	fullPath, err := filepath.Abs(cmdArgs.ConfigFile)
	if err != nil {
		return "", fmt.Errorf("Failed to get absolute path for config file: %v", err)
	}
	filename := strings.TrimSuffix(filepath.Base(fullPath), filepath.Ext(fullPath))
	generatedFilepath := filepath.Join(filepath.Dir(fullPath), filename+generatedConfigFileSuffix)
	err = os.WriteFile(generatedFilepath, []byte(renderedConfig), 0644)
	if err != nil {
		return "", fmt.Errorf("Failed to write generated config file: %v", err)
	}
	return generatedFilepath, nil
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
	ynpSection, ok := ynpConfig["ynp"]
	if !ok {
		ynpSection = make(map[string]any)
		ynpConfig["ynp"] = ynpSection
	}
	if _, ok := ynpSection["use_system_level_systemd"]; !ok {
		ynpSection["use_system_level_systemd"] = false
	}
	if _, ok := ynpSection["configure_thp_settings"]; !ok {
		ynpSection["configure_thp_settings"] = true
	}
	if _, ok := ynpSection["is_ybcontroller_disabled"]; !ok {
		ynpSection["is_ybcontroller_disabled"] = false
	}
	if _, ok := ynpSection["is_install_node_agent"]; !ok {
		ynpSection["is_install_node_agent"] = true
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
	if pArgs.generateConfig || pArgs.generateAndRun {
		// Generate the config file from YBA and exit.
		generatedFilepath, err := generateYnpConfigFileFromYba(ctx, &pArgs.Args)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Failed to generate config file: %v", err)
			return err
		}
		util.ConsoleLogger().Infof(ctx, "Generated config file written to: %s", generatedFilepath)
		if pArgs.generateAndRun {
			pArgs.ConfigFile = generatedFilepath
			util.ConsoleLogger().Infof(ctx, "Continuing with config file: %s", generatedFilepath)
			// Process the arguments again with the generated config file for execution.
			err = processArguments(ctx, pArgs)
			if err != nil {
				util.ConsoleLogger().Errorf(ctx, "Failed to process the command arguments: %v", err)
				return err
			}
		} else {
			return nil
		}
	}
	// Override after INI is read.
	overrideFunc := func(ctx context.Context, iniConfig *config.INIConfig) error {
		defaultValues := iniConfig.DefaultSectionValue()
		userInfo, err := user.Lookup(defaultValues["yb_user"].(string))
		if err == nil {
			defaultValues["yb_user_home"] = userInfo.HomeDir
			defaultValues["yb_user_id"] = userInfo.Uid
		} else if _, ok := err.(user.UnknownUserError); !ok {
			return fmt.Errorf("Failed to look up user: %v\n", err)
		} else if val, ok := defaultValues["yb_user_home"]; !ok || val == "" {
			defaultValues["yb_user_home"] = defaultValues["yb_home_dir"]
		}
		return nil
	}
	iniConfig, err := config.GenerateConfigINI(ctx, &pArgs.Args, overrideFunc)
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
		exitCode := 1
		var scriptErr *command.ScriptExitError
		if errors.As(err, &scriptErr) {
			exitCode = scriptErr.ExitCode
		}
		os.Exit(exitCode)
	}
}
