/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
     "github.com/hashicorp/go-version"
     "fmt"
     "strconv"
     "os"
 )

 func main() {

    commandLineArgs := os.Args[1:]

    type functionPointer func()
    steps := make(map[string][]functionPointer)
    var order []string

    TestSudoPermission()

    if commandLineArgs[0] == "clean" {

        common := Common{"common", "", ""}

        steps[common.Name] = []functionPointer{
            common.Uninstall}

        order = []string{common.Name}

    } else if commandLineArgs[0] == "preflight" {

        Preflight("yba-installer-input-preflight.yml")

    } else if commandLineArgs[0] == "license" {

        License()

    } else if commandLineArgs[0] == "version" {

        Version()

    } else if commandLineArgs[0] == "params" {

        key := commandLineArgs[1]
        value := commandLineArgs[2]

        Params(key, value)

    } else if commandLineArgs[0] == "createBackup" {

        output_path := commandLineArgs[1]

        data_dir := "/opt/yugabyte"
        exclude_prometheus := false
        skip_restart := false
        verbose := false

        createBackupArgs := commandLineArgs[2:]

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

    }  else if commandLineArgs[0] == "restoreBackup" {

        input_path := commandLineArgs[1]

        destination := "/opt/yugabyte"
        skip_restart := false
        verbose := false

        restoreBackupArgs := commandLineArgs[2:]

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

    } else if commandLineArgs[0] == "install" {

        versionToInstall := commandLineArgs[1]
        corsOrigin := commandLineArgs[2]
        httpMode := commandLineArgs[3]

        common := Common{"common", versionToInstall, httpMode}

        steps[common.Name] = []functionPointer{common.SetUpPrereqs,
            common.Uninstall, common.Install}

        var prometheus Prometheus
        v1, _ := version.NewVersion(versionToInstall)
        v2, _ := version.NewVersion("2.8.0.0")

        if v1.LessThan(v2) {
          prometheus = Prometheus{"prometheus",
          "/etc/systemd/system/prometheus.service",
          "/etc/prometheus/prometheus.yml",
          "2.2.1", false}
        } else {
          prometheus = Prometheus{"prometheus",
          "/etc/systemd/system/prometheus.service",
          "/etc/prometheus/prometheus.yml",
          "2.27.1", false}
        }

        steps[prometheus.Name] = []functionPointer{prometheus.SetUpPrereqs,
            prometheus.Install, prometheus.Start}

        postgres := Postgres{"postgres",
        "/usr/lib/systemd/system/postgresql-11.service",
        []string{"/var/lib/pgsql/11/data/pg_hba.conf",
        "/var/lib/pgsql/11/data/postgresql.conf"},
        "11"}

        steps[postgres.Name] = []functionPointer{postgres.SetUpPrereqs,
            postgres.Install, postgres.Restart}

        platform := Platform{"platform",
            "/etc/systemd/system/yb-platform.service",
            "/opt/yugabyte/platform.conf",
            versionToInstall, corsOrigin, false}

        steps[platform.Name] = []functionPointer{platform.Install, platform.Start}

        nginx := Nginx{"nginx",
            "/etc/nginx/nginx.conf",
            httpMode, "_", "", ""}

        steps[nginx.Name] = []functionPointer{nginx.SetUpPrereqs,
            nginx.Install, nginx.Start}

        order = []string{common.Name, prometheus.Name, postgres.Name, platform.Name,
        nginx.Name}

    } else if commandLineArgs[0] == "upgrade" {

        versionToUpgrade := commandLineArgs[1]
        corsOrigin := commandLineArgs[2]
        httpMode := commandLineArgs[3]

        common := Common{"common", versionToUpgrade, httpMode}

        steps[common.Name] = []functionPointer{common.SetUpPrereqs, common.Upgrade}

        prometheus := Prometheus{"prometheus",
        "/etc/systemd/system/prometheus.service",
        "/etc/prometheus/prometheus.yml",
        "2.27.1", false}

        steps[prometheus.Name] = []functionPointer{
            prometheus.Install, prometheus.Start}

        platform := Platform{"platform",
            "/etc/systemd/system/yb-platform.service",
            "/opt/yugabyte/platform.conf",
            versionToUpgrade, corsOrigin, false}

        steps[platform.Name] = []functionPointer{
            platform.Stop, platform.Install, platform.Start}

        nginx := Nginx{"nginx",
            "/etc/nginx/nginx.conf",
            httpMode, "_", "", ""}

        steps[nginx.Name] = []functionPointer{nginx.SetUpPrereqs,
            nginx.Install, nginx.Start}

        order = []string{common.Name, prometheus.Name, platform.Name,
                nginx.Name}

        }

    for _, service := range order {
        serviceSteps := steps[service]
        fmt.Println("Executing steps for action " + commandLineArgs[0] + " for service " +
        service + "!")
        for index, _ := range serviceSteps {
            serviceSteps[index]()
        }
    }
}
