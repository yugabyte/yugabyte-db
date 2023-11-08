/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"os"
	"strings"
	"time"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/releases"
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
		"config file, defaults to $HOME/.yba-cli.yaml")
	rootCmd.PersistentFlags().StringP("host", "H", "http://localhost:9000",
		"YugabyteDB Anywhere Host, defaults to http://localhost:9000")
	rootCmd.PersistentFlags().StringP("apiToken", "a", "", "YugabyteDB Anywhere api token.")
	rootCmd.PersistentFlags().StringP("output", "o", "table",
		"select the desired output format (table, json, pretty), defaults to table.")
	rootCmd.PersistentFlags().StringP("logLevel", "l", "info",
		"select the desired log level format, defaults to info.")
	rootCmd.PersistentFlags().Bool("debug", false, "use debug mode, same as --logLevel debug.")
	rootCmd.PersistentFlags().Bool("no-color", false, "disable colors in output , defaults to false.")
	rootCmd.PersistentFlags().Bool("wait", true,
		"wait until the task is completed, otherwise it will exit immediately, defaults to true.")
	rootCmd.PersistentFlags().Duration("timeout", 7*24*time.Hour,
		"wait command timeout,example: 5m, 1h.")

	//Bind peristents flags to viper
	viper.BindPFlag("host", rootCmd.PersistentFlags().Lookup("host"))
	viper.BindPFlag("apiToken", rootCmd.PersistentFlags().Lookup("apiToken"))
	viper.BindPFlag("output", rootCmd.PersistentFlags().Lookup("output"))
	viper.BindPFlag("logLevel", rootCmd.PersistentFlags().Lookup("logLevel"))
	viper.BindPFlag("debug", rootCmd.PersistentFlags().Lookup("debug"))
	viper.BindPFlag("no-color", rootCmd.PersistentFlags().Lookup("no-color"))
	viper.BindPFlag("wait", rootCmd.PersistentFlags().Lookup("wait"))
	viper.BindPFlag("timeout", rootCmd.PersistentFlags().Lookup("timeout"))

	rootCmd.AddCommand(authCmd)
	rootCmd.AddCommand(releases.ReleasesCmd)
	rootCmd.AddCommand(provider.ProviderCmd)
	rootCmd.AddCommand(universe.UniverseCmd)
	util.AddCommandIfFeatureFlag(rootCmd, tools.ToolsCmd, util.TOOLS)

}

// Execute commands
func Execute(version string) {
	rootCmd.Version = version
	if err := rootCmd.Execute(); err != nil {
		logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
	}
}

func setDefaults() {
	viper.SetDefault("host", "http://localhost:9000")
	viper.SetDefault("output", "table")
	viper.SetDefault("logLevel", "info")
	viper.SetDefault("debug", false)
	viper.SetDefault("no-color", false)
	viper.SetDefault("wait", true)
	viper.SetDefault("timeout", time.Duration(7*24*time.Hour))
	viper.SetDefault("lastVersionAvailable", "v0.0.0")
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
	//Set Logrus formatter options
	log.SetFormatter()
	// Set log level
	log.SetLogLevel(viper.GetString("logLevel"), viper.GetBool("debug"))
	// If a config file is found, read it in.
	if err := viper.ReadInConfig(); err == nil {
		logrus.Debugf("Using config file: %s\n", viper.ConfigFileUsed())
	}

}
