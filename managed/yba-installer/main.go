/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	log "github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"os"
	"strconv"
	"text/tabwriter"
	pre "yba-installer/preflight"
)

var INSTALL_ROOT = GetInstallRoot()

var INSTALL_VERSION_DIR = INSTALL_ROOT + "/yba_installer-" + version

var currentUser = GetCurrentUser()

type functionPointer func()

var steps = make(map[string][]functionPointer)

var order []string

var version = GetVersion()

var serviceManagementMode = getYamlPathData(".serviceManagementMode")

var logLevel = getYamlPathData(".logLevel")

var bringOwnPostgres, errPostgres = strconv.ParseBool(getYamlPathData(".postgres.bringOwn"))

var bringOwnPython, errPython = strconv.ParseBool(getYamlPathData(".python.bringOwn"))

var goBinaryName = "yba-ctl"

var inputFile = "yba-installer-input.yml"

var versionMetadataJson = "version_metadata.json"

var bundledPostgresName = "postgresql-9.6.24-1-linux-x64-binaries.tar.gz"

var yugabundleBinary = "../yugabundle-" + version + "-centos-x86_64.tar.gz"

var javaBinaryName = "OpenJDK8U-jdk_x64_linux_hotspot_8u345b01.tar.gz"

var pemToKeystoreConverter = "pemtokeystore-linux-amd64"

var ports = []string{"5432", "9000", "9090"}

var statusOutput = tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ',
	tabwriter.Debug|tabwriter.AlignRight)

// SYSTEMCTL command we use to start services as root.
var SYSTEMCTL string = "systemctl"

var postgres = Postgres{"Postgres",
	"/etc/systemd/system/postgres.service",
	[]string{INSTALL_ROOT + "/postgres/pgsql/data/pg_hba.conf",
		INSTALL_ROOT + "/postgres/pgsql/data/postgresql.conf"},
	"9.6"}

var prometheus = Prometheus{"Prometheus",
	"/etc/systemd/system/prometheus.service",
	INSTALL_ROOT + "/prometheus/conf/prometheus.yml",
	"2.37.0", false}

var platform = Platform{"Platform",
	"/etc/systemd/system/yb-platform.service",
	INSTALL_ROOT + "/yb-platform/conf/yb-platform.conf",
	version, corsOrigin, false}

var common = Common{"common", version}

var rootCmd = &cobra.Command{
	Use:   "yba-ctl",
	Short: "YBA Installer is used to install Yugabyte Anywhere in an automated manner.",
	Long: `
    YBA Installer is your one stop shop for deploying Yugabyte Anywhere! Through
    YBA Installer, you can perform numerous actions related to your Yugabyte
    Anywhere instance through our command line CLI, such as clean, createBackup,
    restoreBackup, install, and upgrade! View the CLI menu to learn more!`,
}

var statusCmd = &cobra.Command{
	Use: "status",
	Short: "The status command prints out the status of service(s) running as " +
		"part of your Yugabyte Anywhere installation.",
	Long: `
    The status command is used to print out the information corresponding to the
    status of all services related to Yugabyte Anywhere, or for just a particular service.
    For each service, the status command will print out the name of the service, the version of the
    service, the port the service is associated with, the location of any
    applicable systemd and config files, and the running status of the service
    (active or inactive)`,
	Run: func(cmd *cobra.Command, args []string) {

		ValidateArgLength("status", args, 0, 1)

		steps[common.Name] = []functionPointer{common.Status}

		steps[prometheus.Name] = []functionPointer{prometheus.Status}

		steps[postgres.Name] = []functionPointer{postgres.Status}

		steps[platform.Name] = []functionPointer{platform.Status}

		order = []string{common.Name, prometheus.Name,
			postgres.Name, platform.Name}

		if len(args) == 1 {
			if args[0] == "postgres" {
				order = []string{common.Name, postgres.Name}
			} else if args[0] == "prometheus" {
				order = []string{common.Name, prometheus.Name}
			} else if args[0] == "yb-platform" {
				order = []string{common.Name, platform.Name}
			} else {
				LogError("Invalid service name passed in. Valid options: postgres, prometheus, " +
					"yb-platform")
			}
		}

		loopAndExecute("status")

		statusOutput.Flush()

	},
}

var cleanCmd = &cobra.Command{
	Use:   "clean",
	Short: "The clean command uninstalls your Yugabyte Anywhere instance.",
	Long: `
    The clean command performs a complete removal of your Yugabyte Anywhere
    Instance by stopping all services, removing data directories, and dropping the
    Yugabyte Anywhere database.`,
	Run: func(cmd *cobra.Command, args []string) {

		ValidateArgLength("clean", args, -1, 0)

		common := Common{"common", version}

		steps[common.Name] = []functionPointer{
			common.Uninstall}

		order = []string{common.Name}

		loopAndExecute("clean")
	},
}

func preflightCmd() *cobra.Command {
	var skippedPreflightChecks []string
	preflight := &cobra.Command{
		Use: "preflight [list]",
		Short: "The preflight command checks makes sure that your system is ready to " +
			"install Yugabyte Anywhere.",
		Long: `
        The preflight command goes through a series of Preflight checks that each have a
        critcal and warning level, and alerts you if these requirements are not met on your
        Operating System.`,
		Run: func(cmd *cobra.Command, args []string) {
			ValidateArgLength("preflight", args, 0, 1)
			if len(args) == 1 && args[0] == "list" {
				pre.PreflightList()
			} else {
				pre.PreflightChecks("yba-installer-input.yml", skippedPreflightChecks...)
			}
		},
	}

	preflight.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip")

	return preflight
}

var licenseCmd = &cobra.Command{
	Use:   "license",
	Short: "The license command prints out YBA Installer licensing requirements.",
	Long: `
    The license command prints out any licensing requirements associated with
    YBA Installer in order for customers to run it. Currently there are no licensing
    requirements for YBA Installer, but that could change in the future.
    `,
	Run: func(cmd *cobra.Command, args []string) {
		ValidateArgLength("license", args, -1, 0)
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
	Run: func(cmd *cobra.Command, args []string) {
		ValidateArgLength("start", args, 0, 1)
		if len(args) == 1 {
			if args[0] == "postgres" {
				postgres.Start()
			} else if args[0] == "prometheus" {
				prometheus.Start()
			} else if args[0] == "yb-platform" {
				platform.Start()
			} else {
				LogError("Invalid service name passed in. Valid options: postgres, prometheus, " +
					"yb-platform")
			}
		} else {
			postgres.Start()
			prometheus.Start()
			platform.Start()
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
	Run: func(cmd *cobra.Command, args []string) {
		ValidateArgLength("stop", args, 0, 1)
		if len(args) == 1 {
			if args[0] == "postgres" {
				postgres.Stop()
			} else if args[0] == "prometheus" {
				prometheus.Stop()
			} else if args[0] == "yb-platform" {
				platform.Stop()
			} else {
				LogError("Invalid service name passed in. Valid options: postgres, prometheus, " +
					"yb-platform")
			}
		} else {
			postgres.Stop()
			prometheus.Stop()
			platform.Stop()
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
	Run: func(cmd *cobra.Command, args []string) {
		ValidateArgLength("restart", args, 0, 1)
		if len(args) == 1 {
			if args[0] == "postgres" {
				postgres.Restart()
			} else if args[0] == "prometheus" {
				prometheus.Restart()
			} else if args[0] == "yb-platform" {
				platform.Restart()
			} else {
				LogError("Invalid service name passed in. Valid options: postgres, prometheus, " +
					"yb-platform")
			}
		} else {
			postgres.Restart()
			prometheus.Restart()
			platform.Restart()
		}
	},
}

var versionCmd = &cobra.Command{
	Use:   "version",
	Short: "The version command prints out the current version associated with YBA Installer.",
	Long: `
    The version command prints out the current version associated with YBA Installer. It corresponds
    exactly to the version of Yugabyte Anywhere that you will be installing when you invove the yba-ctl
    binary using the install command line option.`,
	Run: func(cmd *cobra.Command, args []string) {
		ValidateArgLength("version", args, -1, 0)
		LogInfo("You are on version " + version +
			" of YBA Installer!")
	},
}

var paramsCmd = &cobra.Command{
	Use:   "params key value",
	Short: "The params command can be used to update entries in the user configuration file.",
	Long: `
    The params command is used to update configuration entries in yba-installer-input.yml,
    corresponding to the settings for your Yugabyte Anywhere installation. Note that invoking
    this command will update your configuration files, but will not restart any services for you.
    Use the reconfigure command for that alternative.`,
	Run: func(cmd *cobra.Command, args []string) {
		ValidateArgLength("params", args, 2, 2)
		// Remove existing crontab entries to update with new configuration settings.
		if !hasSudoAccess() {
			ExecuteBashCommand("bash", []string{"-c", "crontab -r"})
		}

		key := args[0]
		value := args[1]

		Params(key, value)
		GenerateTemplatedConfiguration()

		if !hasSudoAccess() {
			prometheus.CreateCronJob()
			postgres.CreateCronJob()
			platform.CreateCronJob()
		}
	},
}

var reConfigureCmd = &cobra.Command{
	Use: "reconfigure [key] [value]",
	Short: "The reconfigure command updates config entries in yba-installer-input.yml " +
		"if desired, and restarts all Yugabyte Anywhere services.",
	Long: `
    The reconfigure command is used to update configuration entries in the user configuration file
    yba-installer-input.yml, and performs a restart of all Yugabyte Anywhere services to make the
    changes from the updated configuration take effect. It is possible to invoke this method in
    one of two ways. Executing reconfigure without any arguments will perform a simple restart
    of all Yugabyte Anywhere services without any updates to the configuration files. Executing
    reconfigure with a key and value argument pair (the configuration setting you want to update)
    will update the configuration files accordingly, and restart all Yugabyte Anywhere services.
    `,
	Run: func(cmd *cobra.Command, args []string) {

		ExactValidateArgLength("reConfigure", args, []int{0, 2})

		// Remove existing crontab entries to update with new configuration settings.
		if !hasSudoAccess() {
			ExecuteBashCommand("bash", []string{"-c", "crontab -r"})
		}

		if len(args) == 2 {

			key := args[0]
			value := args[1]

			Params(key, value)

		}

		GenerateTemplatedConfiguration()

		steps[postgres.Name] = []functionPointer{postgres.Stop, postgres.Start}

		steps[prometheus.Name] = []functionPointer{prometheus.Stop, prometheus.Start}

		steps[platform.Name] = []functionPointer{platform.Stop, platform.Start}

		order = []string{platform.Name, prometheus.Name}

		if !bringOwnPostgres {

			order = []string{platform.Name, postgres.Name, prometheus.Name}

		}

		loopAndExecute("reconfigure")

		if !hasSudoAccess() {
			prometheus.CreateCronJob()
			postgres.CreateCronJob()
			platform.CreateCronJob()
		}

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
		Run: func(cmd *cobra.Command, args []string) {

			ValidateArgLength("createBackup", args, 1, 1)

			outputPath := args[0]

			CreateBackupScript(outputPath, dataDir, excludePrometheus,
				skipRestart, verbose)
		},
	}

	createBackup.Flags().StringVar(&dataDir, "data_dir", "/opt/yugabyte",
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
		Run: func(cmd *cobra.Command, args []string) {

			ValidateArgLength("restoreBackup", args, 1, 1)

			inputPath := args[0]

			RestoreBackupScript(inputPath, destination, skipRestart, verbose)
		},
	}

	restoreBackup.Flags().StringVar(&destination, "destination", "/opt/yugabyte",
		"where to un-tar the backup")
	restoreBackup.Flags().BoolVar(&skipRestart, "skip_restart", false,
		"don't restart processes during execution (default: false)")
	restoreBackup.Flags().BoolVar(&verbose, "verbose", false,
		"verbose output of script (default: false)")
	return restoreBackup
}

func installCmd() *cobra.Command {
	var skippedPreflightChecks []string
	install := &cobra.Command{
		Use:   "install",
		Short: "The install command is installs Yugabyte Anywhere onto your operating system.",
		Long: `
        The install command is the main workhorse command for YBA Installer that
        will install the version of Yugabyte Anywhere associated with your downloaded version
        of YBA Installer onto your host Operating System. Can also perform an install while skipping
        certain preflight checks if desired.
        `,
		Run: func(cmd *cobra.Command, args []string) {

			ValidateArgLength("install", args, -1, 0)

			if errPostgres != nil {
				LogError("Please set postgres.BringOwn to either true or false before installation.")
			}

			if errPython != nil {
				LogError("Please set python.BringOwn to either true or false before installation!.")
			}

			if bringOwnPostgres {

				if !ValidateUserPostgres("yba-installer-input.yml") {
					LogError("User Postgres not correctly configured! " +
						"Check settings and the above logging message.")
				}

			}

			if bringOwnPython {

				if !ValidateUserPython("yba-installer-input.yml") {

					LogError("User Python not correctly configured! " +
						"Check settings.")
				}
			}

			pre.PreflightChecks("yba-installer-input.yml", skippedPreflightChecks...)

			steps[common.Name] = []functionPointer{common.SetUpPrereqs,
				common.Install}

			steps[prometheus.Name] = []functionPointer{prometheus.SetUpPrereqs,
				prometheus.Install, prometheus.Start}

			steps[postgres.Name] = []functionPointer{postgres.SetUpPrereqs,
				postgres.Install, postgres.Start}

			steps[platform.Name] = []functionPointer{platform.Install,
				platform.Start}

			order = []string{common.Name, prometheus.Name,
				platform.Name}

			if !bringOwnPostgres {

				order = []string{common.Name, prometheus.Name,
					postgres.Name, platform.Name}
			}

			loopAndExecute("install")

			statusCmd.Run(cmd, []string{})

		},
	}

	install.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip")

	return install
}

var upgradeCmd = &cobra.Command{
	Use:   "upgrade",
	Short: "The upgrade command is used to upgrade an existing Yugabyte Anywhere installation.",
	Long: `
   The execution of the upgrade command will upgrade an already installed version of Yugabyte
   Anywhere present on your operating system, to the upgrade version associated with your download of
   YBA Installer. Please make sure that you have installed Yugabyte Anywhere using the install command
   prior to executing the upgrade command.
   `,
	Run: func(cmd *cobra.Command, args []string) {

		ValidateArgLength("upgrade", args, -1, 0)

		// Making sure that an installation has already taken place before an upgrade.
		if _, err := os.Stat(INSTALL_ROOT); err != nil {
			if os.IsNotExist(err) {
				LogError(INSTALL_ROOT + " doesn't exist, did you mean to run sudo ./yba-ctl upgrade?")
			}
		}

		steps[common.Name] = []functionPointer{common.SetUpPrereqs,
			common.Upgrade}

		steps[prometheus.Name] = []functionPointer{prometheus.Stop, prometheus.SetUpPrereqs,
			prometheus.Install, prometheus.Start}

		steps[postgres.Name] = []functionPointer{postgres.Stop, postgres.SetUpPrereqs,
			postgres.Install, postgres.Start}

		steps[platform.Name] = []functionPointer{platform.Stop, platform.Install,
			platform.Start}

		order = []string{common.Name, prometheus.Name, platform.Name}

		if !bringOwnPostgres {

			order = []string{common.Name, prometheus.Name,
				postgres.Name, platform.Name}
		}

		loopAndExecute("upgrade")

		statusCmd.Run(cmd, []string{})

	},
}

func loopAndExecute(action string) {

	for _, service := range order {
		serviceSteps := steps[service]
		if action != "status" {
			LogInfo("Executing steps for action " + action + " for service " +
				service + "!")
		}
		for index, _ := range serviceSteps {
			serviceSteps[index]()
		}
	}

}

func init() {
	rootCmd.AddCommand(cleanCmd, preflightCmd(), licenseCmd, versionCmd,
		paramsCmd, reConfigureCmd, createBackupCmd(), restoreBackupCmd(), installCmd(),
		upgradeCmd, startCmd, stopCmd, restartCmd, statusCmd)

	// Currently only the log message with an info level severity or above are
	// logged (warn, error, fatal, panic).
	// Change the log level to debug for more verbose logging output.
	log.SetFormatter(&log.TextFormatter{
		ForceColors:   true,
		DisableColors: false,
	})

	if logLevel == "TraceLevel" {
		log.SetLevel(log.TraceLevel)
	} else if logLevel == "DebugLevel" {
		log.SetLevel(log.DebugLevel)
	} else if logLevel == "InfoLevel" {
		log.SetLevel(log.InfoLevel)
	} else if logLevel == "WarnLevel" {
		log.SetLevel(log.WarnLevel)
	} else if logLevel == "ErrorLevel" {
		log.SetLevel(log.ErrorLevel)
	} else if logLevel == "FatalLevel" {
		log.SetLevel(log.FatalLevel)
	} else if logLevel == "PanicLevel" {
		log.SetLevel(log.PanicLevel)
	} else {
		LogError("Invalid Logging Level specified in yba-installer-input.yml!")
	}

	log.SetOutput(os.Stdout)
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		LogError(err.Error())
	}
}

func main() {

	Execute()

}
