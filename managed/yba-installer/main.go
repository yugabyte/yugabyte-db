/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
     "fmt"
     "strconv"
     "github.com/spf13/cobra"
     "log"
     "os"
     "strings"
 )

 type functionPointer func()

 var steps = make(map[string][]functionPointer)

 var order []string

 var versionToInstall = GetVersion()

 var versionToUpgrade = GetVersion()

 var httpMode = getYamlPathData(".nginx.mode")

 var serviceManagementMode = getYamlPathData(".serviceManagementMode")

 var bringOwnPostgres, errPostgres = strconv.ParseBool(getYamlPathData(".postgres.bringOwn"))

 var bringOwnPython, errPython = strconv.ParseBool(getYamlPathData(".python.bringOwn"))

 var postgres = Postgres{"postgres",
 "/usr/lib/systemd/system/postgresql-11.service",
 []string{"/var/lib/pgsql/11/data/pg_hba.conf",
 "/var/lib/pgsql/11/data/postgresql.conf"},
 "11"}

 var prometheus = Prometheus{"prometheus",
         "/etc/systemd/system/prometheus.service",
         "/etc/prometheus/prometheus.yml",
         "2.37.0", false}

 var nginx = Nginx{"nginx",
                "/usr/lib/systemd/system/nginx.service",
                "/etc/nginx/nginx.conf",
                httpMode, "_"}

 var platformInstall = Platform{"platform",
            "/etc/systemd/system/yb-platform.service",
            "/opt/yugabyte/platform.conf",
            versionToInstall, corsOrigin, false}

 var platformUpgrade = Platform{"platform",
      "/etc/systemd/system/yb-platform.service",
      "/opt/yugabyte/platform.conf",
      versionToUpgrade, corsOrigin, false}

var commonInstall = Common{"common", versionToInstall, httpMode}

var commonUpgrade = Common{"common", versionToUpgrade, httpMode}

 var rootCmd = &cobra.Command{
    Use:   "yba-installer",
    Short: "yba-installer is used to install Yugabyte Anywhere in an automated manner.",
    Long: `
    yba-installer is your one stop shop for deploying Yugabyte Anywhere! Through
    yba-installer, you can perform numerous actions related to your Yugabyte
    Anywhere instance through our command line CLI, such as clean(), createBackup(),
    restoreBackup(), install(), and upgrade()! View the CLI menu to learn more!`,
 }

 var cleanCmd = &cobra.Command{
    Use:   "clean",
    Short: "The clean command uninstalls your Yugabyte Anywhere instance.",
    Long:  `
    The clean command performs a complete removal of your Yugabyte Anywhere
    Instance by stopping all services, removing data directories, and dropping the
    Yugabyte Anywhere database.

    Invoke as: sudo ./yba-installer clean`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) > 0{
          log.Fatal("The subcommand clean does not take in any arguments!")
       }

       common := Common{"common", "", ""}

       steps[common.Name] = []functionPointer{
           common.Uninstall}

       order = []string{common.Name}

       loopAndExecute("clean")
    },
 }

 var preflightCmd = &cobra.Command{
    Use:   "preflight",
    Short: "The preflight command checks makes sure that your system is ready to " +
    "install Yugabyte Anywhere.",
    Long:  `
    The preflight command goes through a series of Preflight checks that each have a
    critcal and warning level, and alerts you if these requirements are not met on your
    Operating System. Edit yba-installer-input.yml if you wish to override execution
    of warning level preflight checks.

    Invoke as: sudo ./yba-installer preflight`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) > 0{
          log.Fatal("The subcommand preflight does not take in any arguments!")
       }
       Preflight("yba-installer-input.yml")
    },
 }

 var licenseCmd = &cobra.Command{
    Use:   "license",
    Short: "The license command prints out Yba-installer licensing requirements.",
    Long:  `
    The license command prints out any licensing requirements associated with
    yba-installer in order for customers to run it. Currently there are no licensing
    requirements for yba-installer, but that could change in the future.

    Invoke as: sudo ./yba-installer license`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) > 0{
          log.Fatal("The subcommand license does not take in any arguments!")
       }
       License()
    },
 }

 var startCmd = &cobra.Command{
    Use:   "start [serviceName]",
    Short: "The start command is used to start service(s) required for your Yugabyte " +
    "Anywhere installation.",
    Long:  `
    The start command can be invoked to start any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to start all
    services, or invoked with a specific service name to start only that service.
    Valid service names: postgres, prometheus, yb-platform, nginx

    Invoke as: sudo ./yba-installer start (to start all services)
    sudo ./yba-installer start serviceName (to start that particular service)`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) > 1{
          log.Println("Invalid provided arguments: " + strings.Join(args, " "))
          log.Fatal("The subcommand start only takes in one optional argument, the " +
          " service to start!")
       } else if len(args) == 1 {
        if args[0] == "postgres" {
            postgres.Start()
        } else if args[0] == "prometheus" {
            prometheus.Start()
        } else if args[0] == "yb-platform" {
            platformInstall.Start()
        } else if args[0] == "nginx" {
            nginx.Start()
        } else {
            log.Fatal("Invalid service name passed in. Valid options: postgres, prometheus " +
            "yb-platform, nginx")
            }
        } else {
            postgres.Start()
            prometheus.Start()
            platformInstall.Start()
            nginx.Start()
        }
    },
 }

 var stopCmd = &cobra.Command{
    Use:   "stop [serviceName]",
    Short: "The stop command is used to stop service(s) required for your Yugabyte " +
    "Anywhere installation.",
    Long:  `
    The stop command can be invoked to stop any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to stop all
    services, or invoked with a specific service name to stop only that service.
    Valid service names: postgres, prometheus, yb-platform, nginx

    Invoke as: sudo ./yba-installer stop (to stop all services)
    sudo ./yba-installer stop serviceName (to stop that particular service)`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) > 1{
          log.Println("Invalid provided arguments: " + strings.Join(args, " "))
          log.Fatal("The subcommand stop only takes in one optional argument, the " +
          " service to stop!")
       } else if len(args) == 1 {
        if args[0] == "postgres" {
            postgres.Stop()
        } else if args[0] == "prometheus" {
            prometheus.Stop()
        } else if args[0] == "yb-platform" {
            platformInstall.Stop()
        } else if args[0] == "nginx" {
            nginx.Stop()
        } else {
            log.Fatal("Invalid service name passed in. Valid options: postgres, prometheus " +
            "yb-platform, nginx")
        }
        } else {
            postgres.Stop()
            prometheus.Stop()
            platformInstall.Stop()
            nginx.Stop()
        }
    },
 }

 var restartCmd = &cobra.Command{
    Use:   "restart [serviceName]",
    Short: "The restart command is used to restart service(s) required for your Yugabyte " +
    "Anywhere installation.",
    Long:  `
    The restart command can be invoked to stop any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to restart all
    services, or invoked with a specific service name to restart only that service.
    Valid service names: postgres, prometheus, yb-platform, nginx

    Invoke as: sudo ./yba-installer restart (to restart all services)
    sudo ./yba-installer restart serviceName (to restart that particular service)`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) > 1{
          log.Println("Invalid provided arguments: " + strings.Join(args, " "))
          log.Fatal("The subcommand restart only takes in one optional argument, the " +
          " service to stop!")
       } else if len(args) == 1 {
        if args[0] == "postgres" {
            postgres.Restart()
        } else if args[0] == "prometheus" {
            prometheus.Restart()
        } else if args[0] == "yb-platform" {
            platformInstall.Restart()
        } else if args[0] == "nginx" {
            nginx.Restart()
        } else {
            log.Fatal("Invalid service name passed in. Valid options: postgres, prometheus " +
            "yb-platform, nginx")
        }
        } else {
            postgres.Restart()
            prometheus.Restart()
            platformInstall.Restart()
            nginx.Restart()
        }
    },
 }

 var versionCmd = &cobra.Command{
    Use:   "version",
    Short: "The version command prints out the current version associated with yba-installer.",
    Long:  `
    The version command prints out the current version associated with yba-installer. It corresponds
    exactly to the version of Anywhere that you will be installing when you invove the yba-installer
    binary using the install command line option.

    Invoke as: sudo ./yba-installer version`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) > 0{
          log.Fatal("The subcommand license does not take in any arguments!")
       }
       fmt.Println("You are on version " + versionToInstall +
       " of Yba-installer!")
    },
 }

 var paramsCmd = &cobra.Command{
    Use:   "params key value",
    Short: "The params command can be used to update entries in the user configuration file.",
    Long:  `
    The params command is used to update configuration entries in yba-installer-input.yml,
    corresponding to the settings for your Yugabyte Anywhere installation. Note that invoking
    this command will update your configuration files, but will not restart any services for you.
    Use the configure command for that alternative.

    Invoke as: sudo ./yba-installer params key value`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) != 2 {
          log.Println("Invalid provided arguments: " + strings.Join(args, " "))
          log.Fatal("The subcommand params takes in exactly 2 arguments! (the configuration key" +
         " and the value you want to set the key to)")
       }
        key := args[0]
        value := args[1]

        Params(key, value)
        GenerateTemplatedConfiguration()
    },
 }

var reConfigureCmd = &cobra.Command{
Use:   "reconfigure [key] [value]",
Short: "The reconfigure command updates configuration entries in yba-installer-input.yml " +
"if desired, and restarts all Yugabyte Anywhere services.",
Long:  `
The reconfigure command is used to update configuration entries in the user configuration file
yba-installer-input.yml, and performs a restart of all Yugabyte Anywhere services to make the
changes from the updated configuration take effect. It is possible to invoke this method in
one of two ways. Executing reconfigure without any arguments will perform a simple restart of all
Yugabyte Anywhere services without any updates to the configuration files. Executing
reconfigure with a key and value argument pair (the configuration setting you want to update)
will update the configuration files accordingly, and restart all Yugabyte Anywhere services.

Invoke as either: sudo ./yba-installer reconfigure key value (for config updates + restart)
                  sudo ./yba-installer reconfigure (for restart only)
`,
Run: func(cmd *cobra.Command, args []string) {
    if len(args) != 2 && len(args) != 0 {
        log.Println("Invalid provided arguments: " + strings.Join(args, " "))
        log.Fatal("The subcommand reconfigure takes in either no arguments for a simple " +
        "restart, or two arguments for a configuration update! (the configuration key" +
        " and the value you want to set the key to)")
    }

    if (len(args) == 2) {

        key := args[0]
        value := args[1]

        Params(key, value)

      }

    GenerateTemplatedConfiguration()

    steps[postgres.Name] = []functionPointer{postgres.Stop, postgres.Start}

    steps[prometheus.Name] = []functionPointer{prometheus.Stop, prometheus.Start}

    steps[platformInstall.Name] = []functionPointer{platformInstall.Stop, platformInstall.Start}

    steps[nginx.Name] = []functionPointer{nginx.Stop, nginx.Start}

    order = []string{prometheus.Name, platformInstall.Name, nginx.Name}

    if ! bringOwnPostgres {

        order = []string{postgres.Name, prometheus.Name, platformInstall.Name, nginx.Name}

    }

    loopAndExecute("reconfigure")

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
    Long:  `
    The createBackup command executes our yb_platform_backup.sh that creates a backup of your
    Yugabyte Anywhere instance. Executing this command requires that you create and specify the
    outputPath where you want the backup .tar.gz file to be stored as the first argument to
    createBackup. There are also optional flag specifications you can specify for the execution
    of createBackup, which are listed below in the flags section.

    Invoke as: sudo ./yba-installer createBackup outputPath [--data_dir=DIRECTORY]
    [--exclude_prometheus] [--skip_restart] [--verbose]
    `,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) != 1 {
          log.Println("Invalid provided arguments: " + strings.Join(args, " "))
          log.Fatal("The createBackup command takes in exactly one argument, the output path " +
          "where the platform backup is written to! Please specify the output path.")
       }

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
    Long:  `
    The restoreBackup command executes our yb_platform_backup.sh that restores the backup of your
    Yugabyte Anywhere instance. Executing this command requires that you create and specify the
    inputPath where the backup .tar.gz file that will be restored is located as the first argument
    to restoreBackup. There are also optional flag specifications you can specify for the execution
    of restoreBackup, which are listed below in the flags section.

    Invoke as: sudo ./yba-installer restoreBackup inputPath [--destination=DIRECTORY]
    [--skip_restart] [--verbose]
    `,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) != 1 {
          log.Println("Invalid provided arguments: " + strings.Join(args, " "))
          log.Fatal("The restoreBackup command takes in exactly one argument, the input path " +
          "where the platform backup tar gz is located at! Please specify the input path.")
       }

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

var installCmd = &cobra.Command{
   Use:   "install",
   Short: "The install command is used to install Yugabyte Anywhere onto your operating system.",
   Long:  `The install command is the main workhorse command for yba-installer that will install
   the version of Yugabyte Anywhere associated with your downloaded version of yba-installer onto
   your host Operating System. Please make sure that you have edited yba-installer-input.yml to
   specify the mode that you want to run Nginx on (http or https) prior to running the install.

   Invoke as: sudo ./yba-installer install
   `,
   Run: func(cmd *cobra.Command, args []string) {
      if len(args) != 0 {
         log.Fatal("The subcommand install does not take in any arguments!")
      }

      if errPostgres != nil {
        log.Fatal("Please set postgres.BringOwn to either true or false before installation!")
      }

      if errPython != nil {
        log.Fatal("Please set python.BringOwn to either true or false before installation!")
    }

      if bringOwnPostgres {

        if ! ValidateUserPostgres("yba-installer-input.yml") {
            log.Fatalf("User Postgres not correctly configured! " +
                    "Check settings and the above logging message.")
        }

    }

    if bringOwnPython {

        if ! ValidateUserPython("yba-installer-input.yml") {

            log.Fatalf("User Python not correctly configured! " +
            "Check settings.")
         }

     }

        steps[commonInstall.Name] = []functionPointer{commonInstall.SetUpPrereqs,
            commonInstall.Uninstall, commonInstall.Install}

        steps[prometheus.Name] = []functionPointer{prometheus.SetUpPrereqs,
            prometheus.Install, prometheus.Start}

        steps[postgres.Name] = []functionPointer{postgres.SetUpPrereqs,
        postgres.Install, postgres.Start}

        steps[platformInstall.Name] = []functionPointer{platformInstall.Install, platformInstall.Start}

        steps[nginx.Name] = []functionPointer{nginx.SetUpPrereqs,
            nginx.Install, nginx.Start}

        order = []string{commonInstall.Name, prometheus.Name,
                platformInstall.Name, nginx.Name}

        if ! bringOwnPostgres {

            order = []string{commonInstall.Name, prometheus.Name,
                postgres.Name, platformInstall.Name, nginx.Name}
        }

        loopAndExecute("install")

   },
}

var upgradeCmd = &cobra.Command{
   Use:   "upgrade",
   Short: "The upgrade command is used to upgrade an existing Yugabyte Anywhere installation.",
   Long:  `The execution of the upgrade command will upgrade an already installed version of Yugabyte
   Anywhere present on your operating system, to the upgrade version associated with your download of
   yba-installer. Please make sure that you have installed Yugabyte Anywhere using the install command
   prior to executing the upgrade command.

   Invoke as: sudo ./yba-installer upgrade
   `,
   Run: func(cmd *cobra.Command, args []string) {
      if len(args) != 0 {
         log.Fatal("The subcommand upgrade does not take in any arguments!")
      }

      // Making sure that an installation has already taken place before an upgrade.
      if _, err := os.Stat("/opt/yugabyte"); err != nil {
        if os.IsNotExist(err) {
            log.Fatal("Please make sure that you have installed Yugabyte Anywhere before upgrading!")
        }
     }

        steps[commonUpgrade.Name] = []functionPointer{commonUpgrade.SetUpPrereqs, commonUpgrade.Upgrade}

        steps[prometheus.Name] = []functionPointer{
            prometheus.Install, prometheus.Start}

        steps[platformUpgrade.Name] = []functionPointer{
         platformUpgrade.Stop, platformUpgrade.Install, platformUpgrade.Start}

        steps[nginx.Name] = []functionPointer{nginx.SetUpPrereqs,
            nginx.Install, nginx.Start}

        order = []string{commonUpgrade.Name, prometheus.Name, platformUpgrade.Name,
                nginx.Name}

        loopAndExecute("upgrade")

   },
}


 func loopAndExecute(action string) {

    for _, service := range order {
        serviceSteps := steps[service]
        fmt.Println("Executing steps for action " + action + " for service " +
        service + "!")
        for index, _ := range serviceSteps {
            serviceSteps[index]()
        }
    }

}

func init() {
    rootCmd.AddCommand(cleanCmd, preflightCmd, licenseCmd, versionCmd,
    paramsCmd, reConfigureCmd, createBackupCmd(), restoreBackupCmd(), installCmd,
    upgradeCmd, startCmd, stopCmd, restartCmd)
 }

 func Execute() {
    if err := rootCmd.Execute(); err != nil {
       log.Fatal(err)
    }
 }


 func main() {

    TestSudoPermission()

    Execute()

 }
