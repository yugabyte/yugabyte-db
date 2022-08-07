/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
    "fmt"
    "os"
 )

 // CreateBackup Script execution method that directly calls backup.sh
 // (the updated and maintained backup script).

 // Am assuming that an updated backup.sh script will now be provided
 // in the Platform support packages directory that is equivalent to
 // yb_platform_backup.sh in managed.
 func CreateBackupScript(output_path string, data_dir string,
    exclude_prometheus bool, skip_restart bool, verbose bool) {

    fileName := "/opt/yugabyte/packages/backup.sh"
    err := os.Chmod(fileName, 0777)
    if err != nil {
        fmt.Println(err)
    } else {
        fmt.Println("Create Backup Script has now been given executable permissions!")
    }

    command1 := "/bin/sh"
    arg1 := []string{fileName, "backup", "--output", output_path, "--data_dir", data_dir}
    if exclude_prometheus {
        arg1 = append(arg1, "--exclude-prometheus")
    }
    if skip_restart {
        arg1 = append(arg1, "--skip_restart")
    }
    if verbose {
        arg1 = append(arg1, "--verbose")
    }
    ExecuteBashCommand(command1, arg1)
}
