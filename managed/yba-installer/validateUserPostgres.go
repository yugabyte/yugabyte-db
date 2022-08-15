/*
* Copyright (c) YugaByte, Inc.
*/

package main

import (
    "os"
    "strings"
    "errors"
    "log"
)

// Reads info from input config file and sets all template parameters
// for each individual config file (for every component separately)
func ValidateUserPostgres(filename string) (map[string]string, bool) {

    dataPath := getYamlPathData(".postgres.dataPath")
    postmasterPath := getYamlPathData(".postgres.postmasterPath")
    version := getYamlPathData(".postgres.version")
    port := getYamlPathData(".postgres.port")
    pgHbaConf := getYamlPathData(".postgres.pgHbaConf")
    postgresConf := getYamlPathData(".postgres.postgresConf")
    systemdLocation := getYamlPathData(".postgres.systemdLocation")

    //Logging parsed user provided versions and paths for debugging purposes.
    log.Println("User provided Postgres dataPath: " + dataPath)
    log.Println("User provided Postgres postmasterPath: " + postmasterPath)
    log.Println("User provided Postgres version: " + version)
    log.Println("User provided Postgres port: " + port)
    log.Println("User provided Postgres pgHbaConf: " + pgHbaConf)
    log.Println("User provided Postgres postgresConf: " + postgresConf)
    log.Println("User provided Postgres systemdLocation: " + systemdLocation)


    command := "bash"
    args := []string{"-c", "pgrep -u postgres -fa -- -D"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.Replace(output, "\r\n", "", -1)

    splitOutput := strings.Split(outputTrimmed, " ")

    // Postgres not configured properly.
    if (len(splitOutput) != 4) {

        log.Println("User Postgres not initialized properly!")

        return nil, false
    }


    actualDataPath := strings.TrimSuffix(strings.Split(outputTrimmed, " ")[3], "\n")
    actualPostmasterPath := strings.TrimSuffix(strings.Split(outputTrimmed, " ")[1], "\n")
    length := len(strings.Split(actualDataPath, "/"))
    actualVersion := strings.TrimSuffix(strings.Split(actualDataPath, "/")[length - 3], "\n")

    if actualDataPath != dataPath {

        log.Println("User provided Postgres data path does not match actual " +
        "Postgres data path!")

        return nil, false

    }

    if actualPostmasterPath != postmasterPath {

        log.Println("User provided Postgres postmaster path does not match actual " +
        "Postgres postmaster path!")

        return nil, false

    }

    if actualVersion != version {

        log.Println("User provided Postgres version does not match actual" +
        " Postgres version!")

        return nil, false
    }

    command = "bash"
    args = []string{"-c", "sed -n 4p /var/lib/pgsql/" +
    actualVersion + "/data/postmaster.pid"}
    output, _ = ExecuteBashCommand(command, args)

    // output being empty signifies Postgres port not configured properly.
    if (output == "") {

        log.Println("User provided Postgres not running!")

        return nil, false
    }

    actualPort := strings.TrimSuffix(strings.Replace(output, "\r\n", "", -1), "\n")

    if actualPort != port {

        log.Println("User provided Postgres port does not match actual " +
        "Postgres port!")

        return nil, false
    }

    if _, err := os.Stat(pgHbaConf);
    errors.Is(err, os.ErrNotExist) {

        log.Println("User provided Postgres pgHbaConf file does not exist!")

        return nil, false

    }

    if _, err := os.Stat(postgresConf);
    errors.Is(err, os.ErrNotExist) {

        log.Println("User provided Postgres postgresConf file does not exist!")

        return nil, false

    }

    if _, err := os.Stat(systemdLocation);
    errors.Is(err, os.ErrNotExist) {

        log.Println("User provided Postgres systemd file does not exist!")

        return nil, false

    }

    postgresParams := make(map[string] string)
    postgresParams["systemdLocation"] = systemdLocation
    postgresParams["pgHbaConf"] = pgHbaConf
    postgresParams["postgresConf"] = postgresConf
    postgresParams["version"] = actualVersion

    return postgresParams, true
}
