/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/auth"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/auth/ldap"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/auth/oidc"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/backup"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/customer"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/group"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ha"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/release"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/runtimeconfiguration"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/task"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/tools"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/user"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/xcluster"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/log"

	"github.com/common-nighthawk/go-figure"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var (
	cfgFile      string
	cfgDirectory string
)

// rootCmd represents the base command when called without any subcommands
var rootCmd = &cobra.Command{
	Use: "yba",
	Short: "yba - Command line tools to manage your " +
		"YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.",
	Long: `
	YugabyteDB Anywhere is a control plane for managing YugabyteDB universes
	across hybrid and multi-cloud environments, and provides automation and
	orchestration capabilities. YugabyteDB Anywhere CLI provides ease of access
	via the command line.`,

	Run: func(cmd *cobra.Command, args []string) {
		myFigure := figure.NewFigure("yba", "", true)
		myFigure.Print()
		logrus.Printf("\n")
		cmd.Help()
	},

	PersistentPreRun: func(cmd *cobra.Command, args []string) {
		if strings.HasPrefix(cmd.CommandPath(), "yba completion") {
			return
		}
	},
}

// called on module init
func init() {
	cobra.OnInitialize(initConfig)
	cobra.EnableCaseInsensitive = true

	setDefaults()
	rootCmd.PersistentFlags().SortFlags = false
	rootCmd.PersistentFlags().StringVar(&cfgDirectory, "directory", "",
		"Directory containing YBA CLI configuration and generated files. "+
			"If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. "+
			"Defaults to '$HOME/.yba-cli/'.")
	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "",
		"Full path to a specific configuration file for YBA CLI. "+
			"If provided, this takes precedence over the directory specified via --directory, "+
			"and the generated files are added to the same path. "+
			"If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. "+
			"Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.")
	rootCmd.PersistentFlags().StringP("host", "H", "http://localhost:9000",
		"YugabyteDB Anywhere Host")
	rootCmd.PersistentFlags().StringP("apiToken", "a", "", "YugabyteDB Anywhere api token.")
	rootCmd.PersistentFlags().StringP("output", "o", formatter.TableFormatKey,
		"Select the desired output format. Allowed values: table, json, pretty.")
	rootCmd.PersistentFlags().StringP("logLevel", "l", "info",
		"Select the desired log level format. Allowed values: debug, info, warn, error, fatal.")
	rootCmd.PersistentFlags().Bool("debug", false, "Use debug mode, same as --logLevel debug.")
	rootCmd.PersistentFlags().
		Bool("disable-color", false, "Disable colors in output. (default false)")
	rootCmd.PersistentFlags().Bool("wait", true,
		"Wait until the task is completed, otherwise it will exit immediately.")
	rootCmd.PersistentFlags().Duration("timeout", 7*24*time.Hour,
		"Wait command timeout, example: 5m, 1h.")
	rootCmd.PersistentFlags().Bool("insecure", false,
		"Allow insecure connections to YugabyteDB Anywhere."+
			" Value ignored for http endpoints. Defaults to false for https.")
	rootCmd.PersistentFlags().String("ca-cert", "",
		"CA certificate file path for secure connection to YugabyteDB Anywhere. "+
			"Required when the endpoint is https and --insecure is not set.")

	//Bind peristents flags to viper
	viper.BindPFlag("host", rootCmd.PersistentFlags().Lookup("host"))
	viper.BindPFlag("apiToken", rootCmd.PersistentFlags().Lookup("apiToken"))
	viper.BindPFlag("output", rootCmd.PersistentFlags().Lookup("output"))
	viper.BindPFlag("logLevel", rootCmd.PersistentFlags().Lookup("logLevel"))
	viper.BindPFlag("debug", rootCmd.PersistentFlags().Lookup("debug"))
	viper.BindPFlag("disable-color", rootCmd.PersistentFlags().Lookup("disable-color"))
	viper.BindPFlag("wait", rootCmd.PersistentFlags().Lookup("wait"))
	viper.BindPFlag("timeout", rootCmd.PersistentFlags().Lookup("timeout"))
	viper.BindPFlag("insecure", rootCmd.PersistentFlags().Lookup("insecure"))
	viper.BindPFlag("ca-cert", rootCmd.PersistentFlags().Lookup("ca-cert"))

	rootCmd.AddCommand(auth.AuthCmd)
	rootCmd.AddCommand(auth.LoginCmd)
	rootCmd.AddCommand(auth.RegisterCmd)
	rootCmd.AddCommand(auth.HostCmd)
	rootCmd.AddCommand(release.ReleaseCmd)
	rootCmd.AddCommand(provider.ProviderCmd)
	rootCmd.AddCommand(universe.UniverseCmd)
	rootCmd.AddCommand(storageconfiguration.StorageConfigurationCmd)
	rootCmd.AddCommand(backup.BackupCmd)
	rootCmd.AddCommand(task.TaskCmd)
	rootCmd.AddCommand(eit.EITCmd)
	rootCmd.AddCommand(ear.EARCmd)
	rootCmd.AddCommand(runtimeconfiguration.RuntimeConfigurationCmd)
	rootCmd.AddCommand(rbac.RBACCmd)
	rootCmd.AddCommand(user.UserCmd)
	rootCmd.AddCommand(xcluster.XClusterCmd)
	rootCmd.AddCommand(tools.TreeCmd)
	rootCmd.AddCommand(customer.CustomerCmd)
	rootCmd.AddCommand(group.GroupsCmd)
	rootCmd.AddCommand(ldap.LdapCmd)
	rootCmd.AddCommand(ha.HACmd)
	util.AddCommandIfFeatureFlag(rootCmd, tools.ToolsCmd, util.TOOLS)

	addGroupsCmd(rootCmd)
	// Add commands to be marked as preview in the list below
	util.PreviewCommand(
		rootCmd,
		[]*cobra.Command{alert.AlertCmd, oidc.OIDCCmd, telemetryprovider.TelemetryProviderCmd},
	)

}

// Execute commands
func Execute(version string) {
	rootCmd.Version = version
	rootCmd.SetVersionTemplate("YugabyteDB Anywhere CLI (yba) version: {{.Version}}\n")
	if err := rootCmd.Execute(); err != nil {
		// Set log level and formatter for this error
		log.SetLogLevel(viper.GetString("logLevel"), viper.GetBool("debug"))
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

func setDefaults() {
	viper.SetDefault("host", "http://localhost:9000")
	viper.SetDefault("output", formatter.TableFormatKey)
	viper.SetDefault("logLevel", "info")
	viper.SetDefault("debug", false)
	viper.SetDefault("disable-color", false)
	viper.SetDefault("wait", true)
	viper.SetDefault("timeout", time.Duration(7*24*time.Hour))
	viper.SetDefault("lastVersionAvailable", "0.0.0")
	viper.SetDefault("lastCheckedTime", 0)
	viper.SetDefault("insecure", false)
	viper.SetDefault("ca-cert", "")
}

// initConfig reads in config file and ENV variables if set.
func initConfig() {
	if cfgFile != "" {
		// Use config file from the flag.
		viper.SetConfigFile(cfgFile)
	} else if cfgDirectory != "" {
		// Check if the directory exists
		if stat, err := os.Stat(cfgDirectory); err == nil && stat.IsDir() {
			configPath := filepath.Join(cfgDirectory, ".yba-cli.yaml")
			viper.AddConfigPath(cfgDirectory)
			viper.SetConfigType("yaml")
			viper.SetConfigName(".yba-cli")
			viper.SetConfigFile(configPath)
		} else {
			viper.SetDefault("output", formatter.TableFormatKey)
			viper.SetDefault("logLevel", "info")
			viper.SetDefault("debug", false)
			logrus.Fatalf("%s",
				formatter.Colorize(
					"Provided configuration directory does not exist: "+cfgDirectory, formatter.RedColor,
				))
		}
	} else {
		// Find home directory.
		home, err := os.UserHomeDir()
		cobra.CheckErr(err)
		homeDir, err := os.Stat(home)
		if err != nil {
			cobra.CheckErr(err)
		}
		homePerms := homeDir.Mode().Perm()
		os.Mkdir(home+"/.yba-cli", homePerms)
		// Search config in home directory with name ".yba-cli" (without extension).
		viper.AddConfigPath(home + "/.yba-cli")
		viper.SetConfigType("yaml")
		viper.SetConfigName(".yba-cli")
		viper.SetConfigFile(home + "/.yba-cli/.yba-cli.yaml")
	}

	//Will check every environment variable starting with YBA_
	viper.SetEnvPrefix("yba")
	//Read all enviromnent variable that match YBA_ENVNAME
	viper.AutomaticEnv() // read in environment variables that match
	// Set log level and formatter
	log.SetLogLevel(viper.GetString("logLevel"), viper.GetBool("debug"))
	// If a config file is found, read it in.
	if err := viper.ReadInConfig(); err == nil {
		logrus.Debugf("Using config file: %s\n", viper.ConfigFileUsed())
	}

}

func addGroupsCmd(rootCmd *cobra.Command) {

	rootCmd.AddGroup(
		&cobra.Group{
			ID:    "authentication",
			Title: "Authentication Commands",
		},
	)

	auth.AuthCmd.GroupID = "authentication"
	auth.LoginCmd.GroupID = "authentication"
	auth.RegisterCmd.GroupID = "authentication"
	auth.HostCmd.GroupID = "authentication"
	ldap.LdapCmd.GroupID = "authentication"

	rootCmd.AddGroup(
		&cobra.Group{
			ID:    "integration",
			Title: "Integration Commands",
		},
	)

	ear.EARCmd.GroupID = "integration"
	eit.EITCmd.GroupID = "integration"
	provider.ProviderCmd.GroupID = "integration"
	storageconfiguration.StorageConfigurationCmd.GroupID = "integration"

	rootCmd.AddGroup(
		&cobra.Group{
			ID:    "universe",
			Title: "Universe Operation Commands",
		},
	)

	backup.BackupCmd.GroupID = "universe"
	universe.UniverseCmd.GroupID = "universe"
	xcluster.XClusterCmd.GroupID = "universe"

	rootCmd.AddGroup(
		&cobra.Group{
			ID:    "access-management",
			Title: "Access Management Commands",
		},
	)
	customer.CustomerCmd.GroupID = "access-management"
	user.UserCmd.GroupID = "access-management"
	rbac.RBACCmd.GroupID = "access-management"
	group.GroupsCmd.GroupID = "access-management"

	rootCmd.AddGroup(
		&cobra.Group{
			ID:    "advance",
			Title: "Advance Commands",
		},
	)

	runtimeconfiguration.RuntimeConfigurationCmd.GroupID = "advance"
	release.ReleaseCmd.GroupID = "advance"
}
