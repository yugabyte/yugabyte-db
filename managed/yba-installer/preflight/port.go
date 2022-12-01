/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"net"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var port = Port{"port", "warning"}

type Port struct {
	name         string
	warningLevel string
}

func (p Port) Name() string {
	return p.name
}

func (p Port) WarningLevel() string {
	return p.warningLevel
}

func (p Port) Execute() {

	for _, port := range ports {
		_, err := net.Listen("tcp", ":"+port)
		if err != nil {
			log.Fatal("Connecting error: " + err.Error())
		} else {
			log.Info("Connection to port: " + port + " successful.")
		}
	}
}

func init() {
	RegisterPreflightCheck(port)
}
