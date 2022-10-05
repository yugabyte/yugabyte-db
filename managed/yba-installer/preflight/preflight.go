/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
    "strconv"
    "github.com/spf13/viper"
    "fmt"
 )

 var defaultMinCPUs float64 = 4
 var defaultMinMemoryLimit float64 = 15
 var defaultMinSSDStorage float64 = 50

 var ports = []string{"5432", "9000", "9090"}

 var INSTALL_ROOT = GetInstallRoot()

 var currentUser = GetCurrentUser()

 // Preflight Interface created for Preflight check
 // objects.
 type Preflight interface {
    GetName() string
    GetWarningLevel() string
	Execute()
 }

var preflightCheckObjects = []Preflight{python, port, cpu, memory, ssd, root}

 // PreflightList lists all preflight checks currently conducted.
 func PreflightList() {
    LogInfo("Preflight Check List:")
    for _, check := range(preflightCheckObjects) {
        LogInfo(check.GetName() + ": " + check.GetWarningLevel())
    }
 }

 // PreflightChecks conducts all preflight checks except for those specified to be skipped.
 func PreflightChecks(filename string, skipChecks ... string) {

    preflightChecksList := []string{}

    for _, check := range(preflightCheckObjects) {

        preflightChecksList = append(preflightChecksList, check.GetName())

    }

    viper.SetConfigName(filename)
    viper.SetConfigType("yml")
    viper.AddConfigPath(".")
    err := viper.ReadInConfig()

    if err != nil {
        LogError("Error: " + err.Error() + ".")
    }

    preflight := viper.Get("preflight").(map[string]interface{})

    overrideWarning, _ := strconv.ParseBool(fmt.Sprint(preflight["overridewarning"]))

    for _, check := range(skipChecks) {

        if !Contains(preflightChecksList, check) {

            LogError(check + " is not a valid Preflight check! Please use the " +
            "Preflight list command to get all available Preflight checks.")
        }
    }

    // If the config entry has been set to a non true/false value, then we assume that the
    // user does not wish to override warning level preflight checks.

    // Otherwise, warning level checks can be overriden through a user config entry
    // if desired (overrideWarning = True)

    for _, check := range(preflightCheckObjects) {

        if Contains(skipChecks, check.GetName()) {
            if check.GetWarningLevel() == "critical" {

                LogError("The " + check.GetName() + " preflight check is at a critical level " +
                "and cannot be skipped.")
            }
        }

        if !Contains(skipChecks, check.GetName()) {
            if !(overrideWarning && check.GetWarningLevel() == "warning") {
                check.Execute()
            }
        }
    }

 }

 