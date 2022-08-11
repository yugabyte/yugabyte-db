/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
    "log"
    "strconv"
    "strings"
    "github.com/spf13/viper"
    "fmt"
 )

 // Level: Critical
 func checkMinimumVirtualCPUs(min int) {

    command := "bash"
    args := []string{"-c", "cat /proc/cpuinfo | grep processor | wc -l"}
    output, err := ExecuteBashCommand(command, args)
    if err != nil {
        log.Println(err)
    } else {
        numberOfvCPUs, _ := strconv.Atoi(string(output[0]))
        if numberOfvCPUs < min {
            log.Fatalf("System does not meet the requirement of %v Virtual CPUs.", min)
        }
    }
 }

 // Level: Critical
 func checkMinimumMemoryLimit(min float64) {

    command := "grep"
    args := []string{"MemTotal", "/proc/meminfo"}
    output, err := ExecuteBashCommand(command, args)
    if err != nil {
        log.Println(err)
    } else {
        field1 := strings.Fields(output)[1]
        availableMemoryKB, _ := strconv.Atoi(strings.Split(field1, " ")[0])
        availableMemoryGB := float64(availableMemoryKB) / 1e6
        if availableMemoryGB < min {
            log.Fatalf("System does not meet the minimum memory limit of %v GB.", min)
        }
    }

 }

 // Level: Warning
 func checkMinmumAvailableSSDStorage(min float64) {

    command := "df"
    args := []string{"-H", "--total"}
    output, err := ExecuteBashCommand(command, args)
    if err != nil {
        log.Println(err)
    } else {
        totalIndex := IndexOf(strings.Fields(output), "total")
        sto_str := strings.Split(strings.Fields(output)[totalIndex+1], " ")[0]
        units := string(sto_str[len(sto_str)-1])
        availableSSDstorage, _ := strconv.ParseFloat(sto_str[:len(sto_str)-1], 64)
        if units == "T" {
            availableSSDstorage *= 1024
        }
        if availableSSDstorage < min {
            log.Fatalf("System does not meet the minimum available SSD storage of %v GB.", min)
        }
    }
 }

 func Preflight(filename string) {

    // Critical level checks are performed in all executions of Preflight().
    checkMinimumVirtualCPUs(4)
    checkMinimumMemoryLimit(15)

    viper.SetConfigName(filename)
    viper.SetConfigType("yml")
    viper.AddConfigPath(".")
    err := viper.ReadInConfig()

    if err != nil {
        panic(err)
    }

    preflight := viper.Get("preflight").(map[string]interface{})

    overrideWarning, err := strconv.ParseBool(fmt.Sprint(preflight["overridewarning"]))

    // If the config entry has been set to a non true/false value, then we assume that the
    // user does not wish to override warning level preflight checks.

    // Otherwise, warning level checks can be overriden through a user config entry
    // if desired (overrideWarning = True)

    if err != nil || ! overrideWarning {

        checkMinmumAvailableSSDStorage(50)

    }

 }
