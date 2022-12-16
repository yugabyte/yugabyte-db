/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

// List of services required for YBA installation.
var services map[string]common.Component
var serviceOrder []string

// Service Names
const (
	YbPlatformServiceName string = "yb-platform"
	PostgresServiceName   string = "postgres"
	PrometheusServiceName string = "prometheus"
)

var force bool
var logLevel string

var rootCmd = &cobra.Command{
	Use:   "yba-ctl",
	Short: "YBA Installer is used to install Yugabyte Anywhere in an automated manner.",
	Long: `
    YBA Installer is your one stop shop for deploying Yugabyte Anywhere! Through
    YBA Installer, you can perform numerous actions related to your Yugabyte
    Anywhere instance through our command line CLI, such as clean, createBackup,
    restoreBackup, install, and upgrade! View the CLI menu to learn more!`,
	PersistentPreRun: func(cmd *cobra.Command, args []string) {
		if len(logLevel) == 0 {
			logLevel = viper.GetString("logLevel")
		}
		log.Init(logLevel)
	},
}

func cleanCmd() *cobra.Command {
	var removeData bool
	clean := &cobra.Command{
		Use:   "clean",
		Short: "The clean command uninstalls your Yugabyte Anywhere instance.",
		Long: `
    	The clean command performs a complete removal of your Yugabyte Anywhere
    	Instance by stopping all services and (optionally) removing data directories.`,
		Args: cobra.MaximumNArgs(1),
		Run: func(cmd *cobra.Command, args []string) {

			// TODO: Only clean up per service.
			// Clean up services in reverse order.
			for i := len(serviceOrder) - 1; i >= 0; i-- {
				services[serviceOrder[i]].Uninstall(removeData)
			}

			common.Uninstall()
		},
	}
	clean.Flags().BoolVar(&removeData, "all", false, "also clean out data (default: false)")
	return clean

}

var licenseCmd = &cobra.Command{
	Use:   "license",
	Short: "The license command prints out YBA Installer licensing requirements.",
	Long: `
    The license command prints out any licensing requirements associated with
    YBA Installer in order for customers to run it. Currently there are no licensing
    requirements for YBA Installer, but that could change in the future.
    `,
	Args: cobra.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		License()
	},
}

var startCmd = &cobra.Command{
	Use: "start [serviceName]",
	Short: "The start command is used to start service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The start command can be invoked to start any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to start all
    services, or invoked with a specific service name to start only that service.
    Valid service names: postgres, prometheus, yb-platform`,
	Args:      cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	ValidArgs: serviceOrder,
	Run: func(cmd *cobra.Command, args []string) {
		if len(args) == 1 {
			services[args[0]].Start()
		} else {
			for _, name := range serviceOrder {
				services[name].Start()
			}
		}
	},
}

var stopCmd = &cobra.Command{
	Use: "stop [serviceName]",
	Short: "The stop command is used to stop service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The stop command can be invoked to stop any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to stop all
    services, or invoked with a specific service name to stop only that service.
    Valid service names: postgres, prometheus, yb-platform`,
	Args:      cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	ValidArgs: serviceOrder,
	Run: func(cmd *cobra.Command, args []string) {
		if len(args) == 1 {
			services[args[0]].Stop()
		} else {
			for _, name := range serviceOrder {
				services[name].Stop()
			}
		}
	},
}

var restartCmd = &cobra.Command{
	Use: "restart [serviceName]",
	Short: "The restart command is used to restart service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The restart command can be invoked to stop any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to restart all
    services, or invoked with a specific service name to restart only that service.
    Valid service names: postgres, prometheus, yb-platform`,
	Args:      cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	ValidArgs: serviceOrder,
	Run: func(cmd *cobra.Command, args []string) {
		if len(args) == 1 {
			services[args[0]].Restart()
		} else {
			for _, name := range serviceOrder {
				services[name].Restart()
			}
		}
	},
}

var versionCmd = &cobra.Command{
	Use:   "version",
	Short: "The version of YBA Installer.",
	Args:  cobra.NoArgs,
	Long: `
    The version will be the same as the version of Yugabyte Anywhere that you will be
	installing when you invove the yba-ctl binary using the install command line option.`,
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println(common.GetVersion())
	},
}

func createBackupCmd() *cobra.Command {
	var dataDir string
	var excludePrometheus bool
	var skipRestart bool
	var verbose bool

	createBackup := &cobra.Command{
		Use:   "createBackup outputPath",
		Short: "The createBackup command is used to take a backup of your Yugabyte Anywhere instance.",
		Long: `
    The createBackup command executes our yb_platform_backup.sh that creates a backup of your
    Yugabyte Anywhere instance. Executing this command requires that you create and specify the
    outputPath where you want the backup .tar.gz file to be stored as the first argument to
    createBackup.
    `,
		Args: cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {

			outputPath := args[0]

			if plat, ok := services["yb-platform"].(Platform); ok {
				CreateBackupScript(outputPath, dataDir, excludePrometheus, skipRestart, verbose, plat)
			} else {
				log.Fatal("Could not cast service to Platform struct.")
			}
		},
	}

	createBackup.Flags().StringVar(&dataDir, "data_dir", common.GetBaseInstall(),
		"data directory to be backed up")
	createBackup.Flags().BoolVar(&excludePrometheus, "exclude_prometheus", false,
		"exclude prometheus metric data from backup (default: false)")
	createBackup.Flags().BoolVar(&skipRestart, "skip_restart", false,
		"don't restart processes during execution (default: false)")
	createBackup.Flags().BoolVar(&verbose, "verbose", false,
		"verbose output of script (default: false)")
	return createBackup
}

func restoreBackupCmd() *cobra.Command {
	var destination string
	var skipRestart bool
	var verbose bool

	restoreBackup := &cobra.Command{
		Use:   "restoreBackup inputPath",
		Short: "The restoreBackup command restores a backup of your Yugabyte Anywhere instance.",
		Long: `
    The restoreBackup command executes our yb_platform_backup.sh that restores the backup of your
    Yugabyte Anywhere instance. Executing this command requires that you create and specify the
    inputPath where the backup .tar.gz file that will be restored is located as the first argument
    to restoreBackup.
    `,
		Args: cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {

			inputPath := args[0]

			// TODO: backupScript is the only reason we need to have this cast. Should probably refactor.
			if plat, ok := services["yb-platform"].(Platform); ok {
				RestoreBackupScript(inputPath, destination, skipRestart, verbose, plat)
			} else {
				log.Fatal("Could not cast service to Platform for backup script execution.")
			}

		},
	}

	restoreBackup.Flags().StringVar(&destination, "destination", common.GetBaseInstall(),
		"where to un-tar the backup")
	restoreBackup.Flags().BoolVar(&skipRestart, "skip_restart", false,
		"don't restart processes during execution (default: false)")
	restoreBackup.Flags().BoolVar(&verbose, "verbose", false,
		"verbose output of script (default: false)")
	return restoreBackup
}

func init() {
	// services is an ordered map so services that depend on others should go later in the chain.
	services = make(map[string]common.Component)
	services[PostgresServiceName] = NewPostgres("9.6")
	services[PrometheusServiceName] = NewPrometheus("2.39.0")
	services[YbPlatformServiceName] = NewPlatform(common.GetVersion())
	// serviceOrder = make([]string, len(services))
	serviceOrder = []string{PostgresServiceName, PrometheusServiceName, YbPlatformServiceName}
	// populate names of services for valid args

	rootCmd.AddCommand(cleanCmd(), licenseCmd, versionCmd,
		createBackupCmd(), restoreBackupCmd(),
		upgradeCmd, startCmd, stopCmd, restartCmd, statusCmd)
	rootCmd.PersistentFlags().BoolVarP(&force, "force", "f", false, "skip user confirmation")
	rootCmd.PersistentFlags().StringVar(&logLevel, "log_level", "", "log level for this command")

}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		log.Fatal(err.Error())
	}
}
