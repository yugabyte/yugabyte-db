/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"github.com/spf13/cobra"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Service Names
const (
	YbPlatformServiceName string = "yb-platform"
	PostgresServiceName   string = "postgres"
	PrometheusServiceName string = "prometheus"
	YbdbServiceName       string = "ybdb"
)

var (
	force             bool
	logLevel          string
	skipVersionChecks bool = false
)

var rootCmd = &cobra.Command{
	Use:   "yba-ctl",
	Short: "YBA Installer is used to install YugabyteDB Anywhere in an automated manner.",
	Long: `
    YBA Installer is your one stop shop for deploying YugabyteDB Anywhere! Through
    YBA Installer, you can perform numerous actions related to your Yugabyte
    Anywhere instance through our command line CLI, such as clean, createBackup,
    restoreBackup, install, and upgrade! View the CLI menu to learn more!`,
	PersistentPreRun: func(cmd *cobra.Command, args []string) {
		initAfterFlagsParsed(cmd.CommandPath())
	},
}

// called on module init
func init() {
	rootCmd.PersistentFlags().BoolVarP(&force, "force", "f", false,
		"Run in non-interactive mode. All user confirmations are skipped.")
	rootCmd.PersistentFlags().StringVar(&logLevel, "log_level", "info", "log level for this command."+
		" Levels: panic, fatal, error, warn, info, debug, trace.")
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		log.Fatal(err.Error())
	}
}
