/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import (
    "os"
    "strings"
 )

// GetVersion gets the version at execution time so that yba-installer
// installs the correct version of Yugabyte Anywhere.
 func GetVersion() (string){

   cwd, _ := os.Getwd()
   currentFolderPathList := strings.Split(cwd, "/")
   lenFolder := len(currentFolderPathList)
   currFolder := currentFolderPathList[lenFolder - 1]

   versionInformation := strings.Split(currFolder, "-")

   versionNumber := versionInformation[1]
   buildNumber := versionInformation[2]

   version := versionNumber + "-" + buildNumber

   return version
}
