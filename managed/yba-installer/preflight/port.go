/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "net"
	 "fmt"
  )
 
 var port = Port{"port", "warning"}
 
 type Port struct {
	 Name string
	 WarningLevel string
 }
 
 func (p Port) GetName() string {
	 return p.Name
 }
 
 func (p Port) GetWarningLevel() string {
	 return p.WarningLevel
 }
 
 func (p Port) Execute() {
 
	for _, port := range ports {
        _, err := net.Listen("tcp", ":" + port)
        if err != nil {
            LogError(fmt.Sprintf("Connecting error: ", err))
        } else {
            LogInfo("Connection to port: " + port + " successful.")
        }
    }
 }
