/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"net"
	"strconv"

	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var Port = &portCheck{"port", "warning"}

type portCheck struct {
	name         string
	warningLevel string
}

func (p portCheck) Name() string {
	return p.name
}

func (p portCheck) WarningLevel() string {
	return p.warningLevel
}

func (p portCheck) Execute() error {
	var ports []int = []int{
		viper.GetInt("prometheus.externalPort"),
		viper.GetInt("platform.externalPort"),
		viper.GetInt("platform.containerExposedPort"),
		viper.GetInt("postgres.port"),
	}

	usedPorts := make([]int, len(ports))
	for _, port := range ports {
		_, err := net.Listen("tcp", ":"+strconv.Itoa(port))
		if err != nil {
			usedPorts = append(usedPorts, port)
		} else {
			log.Info(fmt.Sprintf("Connection to port: %d successful.", port))
		}
	}
	if len(usedPorts) > 0 {
		return fmt.Errorf("could not connect to port(s) %v", usedPorts)
	}
	return nil
}
