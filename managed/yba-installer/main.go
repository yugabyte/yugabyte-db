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
 )

 type functionPointer func()

 var steps = make(map[string][]functionPointer)

 var order []string

 var versionToInstall = "2.8.1.0-b37"

 var versionToUpgrade = "2.15.0.1-b4"

 var httpMode = getYamlPathData(".nginx.mode")

 var bringOwnPostgres, errPostgres = strconv.ParseBool(getYamlPathData(".postgres.bringOwn"))

 var bringOwnPython, errPython = strconv.ParseBool(getYamlPathData(".python.bringOwn"))

// Will not be running as a Systemd service, so no service file path
// needed when using bundled postgres.
var postgres = Postgres{"postgres",
"",
[]string{"/var/lib/pgsql/data/pg_hba.conf",
"/var/lib/pgsql/data/postgresql.conf"},
"9.6"}

 var prometheus = Prometheus{"prometheus",
         "/etc/systemd/system/prometheus.service",
         "/etc/prometheus/prometheus.yml",
         "2.27.1", false}

 var nginx = Nginx{"nginx",
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
       Version("version_metadata.json")
    },
 }

 var paramsCmd = &cobra.Command{
    Use:   "params",
    Short: "The params command can be used to update entries in the user configuration file.",
    Long:  `
    The params command is used to update configuration entries in yba-installer-input.yml,
    corresponding to the settings for your Yugabyte Anywhere installation. Note that invoking
    this command will update your configuration files, but will not restart any services for you.
    Use the configure command for that alternative.

    Invoke as: sudo ./yba-installer params key value`,
    Run: func(cmd *cobra.Command, args []string) {
       if len(args) != 2 {
          log.Fatal("The subcommand params takes in exactly 2 arguments! (the configuration key" +
         " and the value you want to set the key to)")
       }
        key := args[0]
        value := args[1]

        Params(key, value)
        GenerateTemplatedConfiguration()
    },
 }

 var configureCmd = &cobra.Command{
   Use:   "configure",
   Short: "The configure command updates configuration entries in yba-installer-input.yml " +
   "if desired, and restarts all Yugabyte Anywhere services.",
   Long:  `
   The configure command can be used to update configuration entries in yba-installer-input.yml,
   corresponding to the settings for your Yugabyte Anywhere installation. Invoking this
   command will also restart all of the associated Yugabyte Anywhere services. Can be invoked
   with a key and value setting(similar to params) for configuration updates prior to the restart,
   or with no arguments for just a simple service restart.

   Invoke as either: sudo ./yba-installer configure key value (for configuration updates + restart)
                     sudo ./yba-installer configure (for restart only)
   `,
   Run: func(cmd *cobra.Command, args []string) {
      if len(args) != 2 && len(args) != 0 {
         log.Fatal("The subcommand configure takes in either no arguments for a simple restart, or two " +
         "arguments for a configuration update! (the configuration key" +
         " and the value you want to set the key to)")
      }

       if (len(args) == 2) {

         key := args[0]
         value := args[1]

         Params(key, value)

       }

       GenerateTemplatedConfiguration()

       steps[postgres.Name] = []functionPointer{postgres.StopBundled, postgres.StartBundled}

       steps[prometheus.Name] = []functionPointer{prometheus.Stop, prometheus.Start}

       steps[platformInstall.Name] = []functionPointer{platformInstall.Stop, platformInstall.Start}

       steps[nginx.Name] = []functionPointer{nginx.Stop, nginx.Start}

       order = []string{postgres.Name, prometheus.Name, platformInstall.Name,
         nginx.Name}

      loopAndExecute("configure")

   },
}

var createBackupCmd = &cobra.Command{
   Use:   "createBackup",
   Short: "The createBackup command is used to take a backup of your Yugabyte Anywhere instance.",
   Long:  `
   The createBackup command executes our createBackup() script that creates a backup of your
   Yugabyte Anywhere instance. Executing this command requires that you create and specify the
   output_path where you want the backup .tar.gz file to be stored. Optional specifications in
   this command's execution are data_dir (data directory to be backed up, default /opt/yugabyte),
   exclude_prometheus (boolean as to whether you want to exclude Prometheus metrics from the
   backup, default false), skip_restart (boolean as to whether you want to skip restarting the
   Yugabyte Anywhere services after taking the backup, default false), and verbose (boolean as
   to whether you want verbose log messages during the createBackup script's execution, default
   false)

   Invoke as: sudo ./yba-installer createBackup output_path [data_dir=/opt/yugabyte]
   [exclude_prometheus=false] [skip_restart=false] [verbose=false]`,
   Run: func(cmd *cobra.Command, args []string) {
      if len(args) < 1 || len(args) > 5 {
         log.Fatal("The arguments for the createBackup command have been improperly specified. " +
         "Please refer to the help menu description for more information.")
      }
        output_path := args[0]

        data_dir := "/opt/yugabyte"
        exclude_prometheus := false
        skip_restart := false
        verbose := false

        createBackupArgs := args[1:]

        if len(createBackupArgs) == 4 {
            data_dir = createBackupArgs[0]
            exclude_prometheus, _ = strconv.ParseBool(createBackupArgs[1])
            skip_restart, _ = strconv.ParseBool(createBackupArgs[2])
            verbose, _ = strconv.ParseBool(createBackupArgs[3])

        } else if len(createBackupArgs) == 3 {
            data_dir = createBackupArgs[0]
            exclude_prometheus, _ = strconv.ParseBool(createBackupArgs[1])
            skip_restart, _ = strconv.ParseBool(createBackupArgs[2])

        } else if len(createBackupArgs) == 2 {
            data_dir = createBackupArgs[0]
            exclude_prometheus, _ = strconv.ParseBool(createBackupArgs[1])

        } else if len(createBackupArgs) == 1 {
            data_dir = createBackupArgs[0]

        }
        CreateBackupScript(output_path, data_dir, exclude_prometheus,
            skip_restart, verbose)
   },
}

var restoreBackupCmd = &cobra.Command{
   Use:   "restoreBackup",
   Short: "The restoreBackup command is used to restore a backup of your Yugabyte Anywhere instance.",
   Long:  `
   The restoreBackup command executes our restoreBackup() script that creates a backup of your
   Yugabyte Anywhere instance. Executing this command requires that you create and specify the
   input path where the backup .tar.gz file that will be restored is located. Optional specifications in
   this command's execution are destination (directory you want the backup restored to,
   default /opt/yugabyte), skip_restart (boolean as to whether you want to skip restarting the
   Yugabyte Anywhere services after restoring the backup, default false), and verbose (boolean as
   to whether you want verbose log messages during the restoreBackup script's execution, default
   false)

   Invoke as: sudo ./yba-installer restoreBackup input_path [destination=/opt/yugabyte]
   [skip_restart=false] [verbose=false]`,
   Run: func(cmd *cobra.Command, args []string) {
      if len(args) < 1 || len(args) > 4 {
         log.Fatal("The arguments for the restoreBackup command have been improperly specified. " +
         "Please refer to the help menu description for more information.")
      }

        input_path := args[0]

        destination := "/opt/yugabyte"
        skip_restart := false
        verbose := false

        restoreBackupArgs := args[1:]

        if len(restoreBackupArgs) == 3 {
            destination = restoreBackupArgs[0]
            skip_restart, _ = strconv.ParseBool(restoreBackupArgs[1])
            verbose, _ = strconv.ParseBool(restoreBackupArgs[2])

        } else if len(restoreBackupArgs) == 2 {
            destination = restoreBackupArgs[0]
            skip_restart, _ = strconv.ParseBool(restoreBackupArgs[1])

        } else if len(restoreBackupArgs) == 1 {
            destination = restoreBackupArgs[0]
        }

        RestoreBackupScript(input_path, destination,
            skip_restart, verbose)
   },
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

        postgresParams, valid := ValidateUserPostgres("yba-installer-input.yml")

        if valid {
            postgres = Postgres{"postgres",
            postgresParams["systemdLocation"],
            []string{postgresParams["pgHbaConf"],
                postgresParams["postgresConf"]},
                postgresParams["version"]}
        } else {

            log.Fatalf("User Postgres not correctly configured! " +
                    "Check settings.")
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

        if ! bringOwnPostgres {

            // InstallBundled will automatically start Postgres in the setup stage.
            steps[postgres.Name] = []functionPointer{postgres.SetUpPrereqsBundled,
                postgres.InstallBundled}

        }

        steps[platformInstall.Name] = []functionPointer{platformInstall.Install, platformInstall.Start}

        steps[nginx.Name] = []functionPointer{nginx.SetUpPrereqs,
            nginx.Install, nginx.Start}

        if ! bringOwnPostgres {

            order = []string{commonInstall.Name, prometheus.Name,
                postgres.Name, platformInstall.Name, nginx.Name}
        }  else {

            order = []string{commonInstall.Name, prometheus.Name,
                platformInstall.Name, nginx.Name}

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
    paramsCmd, configureCmd, createBackupCmd, restoreBackupCmd, installCmd,
    upgradeCmd)
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
