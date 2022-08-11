/*
 * Copyright (c) YugaByte, Inc.
 */

 package main

 import ()

 // Component interface used by all services and
 // the Common class (general operations not performed
 // specific to a service).
 type component interface {
    SetUpPrereqs()
    Install()
    Start()
    Stop()
    Restart()
    GetSystemdFile()
    GetConfFile()
    Uninstall()
    VersionInfo()
    Upgrade()
 }
