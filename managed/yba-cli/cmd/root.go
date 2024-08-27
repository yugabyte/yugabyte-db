/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"os"
	"strings"
	"time"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/auth"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/backup"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/releases"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/task"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/tools"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/log"

	"github.com/common-nighthawk/go-figure"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var (
	cfgFile string
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
	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "",
		"Config file, defaults to $HOME/.yba-cli.yaml")
	rootCmd.PersistentFlags().StringP("host", "H", "http://localhost:9000",
		"YugabyteDB Anywhere Host")
	rootCmd.PersistentFlags().StringP("apiToken", "a", "", "YugabyteDB Anywhere api token.")
	rootCmd.PersistentFlags().StringP("output", "o", "table",
		"Select the desired output format. Allowed values: table, json, pretty.")
	rootCmd.PersistentFlags().StringP("logLevel", "l", "info",
		"Select the desired log level format. Allowed values: debug, info, warn, error, fatal.")
	rootCmd.PersistentFlags().Bool("debug", false, "Use debug mode, same as --logLevel debug.")
	rootCmd.PersistentFlags().Bool("disable-color", false, "Disable colors in output. (default false)")
	rootCmd.PersistentFlags().Bool("wait", true,
		"Wait until the task is completed, otherwise it will exit immediately.")
	rootCmd.PersistentFlags().Duration("timeout", 7*24*time.Hour,
		"Wait command timeout, example: 5m, 1h.")

	//Bind peristents flags to viper
	viper.BindPFlag("host", rootCmd.PersistentFlags().Lookup("host"))
	viper.BindPFlag("apiToken", rootCmd.PersistentFlags().Lookup("apiToken"))
	viper.BindPFlag("output", rootCmd.PersistentFlags().Lookup("output"))
	viper.BindPFlag("logLevel", rootCmd.PersistentFlags().Lookup("logLevel"))
	viper.BindPFlag("debug", rootCmd.PersistentFlags().Lookup("debug"))
	viper.BindPFlag("disable-color", rootCmd.PersistentFlags().Lookup("disable-color"))
	viper.BindPFlag("wait", rootCmd.PersistentFlags().Lookup("wait"))
	viper.BindPFlag("timeout", rootCmd.PersistentFlags().Lookup("timeout"))

	rootCmd.AddCommand(auth.AuthCmd)
	rootCmd.AddCommand(auth.LoginCmd)
	rootCmd.AddCommand(auth.RegisterCmd)
	rootCmd.AddCommand(releases.ReleasesCmd)
	rootCmd.AddCommand(provider.ProviderCmd)
	rootCmd.AddCommand(universe.UniverseCmd)
	rootCmd.AddCommand(storageconfiguration.StorageConfigurationCmd)
	rootCmd.AddCommand(backup.BackupCmd)
	rootCmd.AddCommand(task.TaskCmd)
	rootCmd.AddCommand(eit.EITCmd)
	util.AddCommandIfFeatureFlag(rootCmd, tools.ToolsCmd, util.TOOLS)

	// Example for adding preview commands to the list of available commands
	// util.AddCommandIfFeatureFlag(rootCmd, exampleCmd, util.PREVIEW)

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
	viper.SetDefault("output", "table")
	viper.SetDefault("logLevel", "info")
	viper.SetDefault("debug", false)
	viper.SetDefault("disable-color", false)
	viper.SetDefault("wait", true)
	viper.SetDefault("timeout", time.Duration(7*24*time.Hour))
	viper.SetDefault("lastVersionAvailable", "0.0.0")
	viper.SetDefault("lastCheckedTime", 0)
}

// initConfig reads in config file and ENV variables if set.
func initConfig() {
	if cfgFile != "" {
		// Use config file from the flag.
		viper.SetConfigFile(cfgFile)
	} else {
		// Find home directory.
		home, err := os.UserHomeDir()
		cobra.CheckErr(err)

		// Search config in home directory with name ".yba-cli" (without extension).
		viper.AddConfigPath(home)
		viper.SetConfigType("yaml")
		viper.SetConfigName(".yba-cli")
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
