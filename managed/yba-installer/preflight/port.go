/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "net"
	 "fmt"

     log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
  )

 var port = Port{"port", "warning"}

 type Port struct {
	 name string
	 WarningLevel string
 }

 func (p Port) Name() string {
	 return p.name
 }

 func (p Port) GetWarningLevel() string {
	 return p.WarningLevel
 }

 func (p Port) Execute() {

	for _, port := range ports {
        _, err := net.Listen("tcp", ":" + port)
        if err != nil {
            log.Fatal(fmt.Sprintf("Connecting error: ", err))
        } else {
            log.Info("Connection to port: " + port + " successful.")
        }
    }
 }
