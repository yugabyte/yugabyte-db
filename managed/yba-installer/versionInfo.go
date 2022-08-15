/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
    "fmt"
    "github.com/spf13/viper"
 )

 func Version(versionMetadataFileName string) {

    viper.SetConfigName(versionMetadataFileName)
    viper.SetConfigType("json")
    viper.AddConfigPath(".")
    err := viper.ReadInConfig()
    if err != nil {
        panic(err)
    }

    versionNumber := fmt.Sprint(viper.Get("version_number"))
    buildNumber := fmt.Sprint(viper.Get("build_number"))

    version := versionNumber + "-" + buildNumber

    fmt.Println("You are on version " + version + " of yba-installer!")

 }
