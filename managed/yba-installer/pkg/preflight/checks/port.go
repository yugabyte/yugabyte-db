/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"net"
	"strconv"

	"github.com/spf13/viper"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Port check initialized
var Port = &portCheck{"port", false}

type portCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (p portCheck) Name() string {
	return p.name
}

// SkipAllowed gets if the check can be skipped.
func (p portCheck) SkipAllowed() bool {
	return p.skipAllowed
}

// Execute validates all necessary ports are available.
func (p portCheck) Execute() Result {
	res := Result{
		Check:  p.name,
		Status: StatusPassed,
	}
	var ports []int = []int{
		viper.GetInt("prometheus.port"),
		viper.GetInt("platform.port"),
	}

	if viper.GetBool("postgres.install.enabled") {
		ports = append(ports, viper.GetInt("postgres.install.port"))
	}

	usedPorts := make([]int, 0, len(ports))
	for _, port := range ports {
		listener, err := net.Listen("tcp", ":"+strconv.Itoa(port))
		if err != nil {
			usedPorts = append(usedPorts, port)
		} else {
			log.Info(fmt.Sprintf("Connection to port: %d successful.", port))
		}
		if listener != nil {
			listener.Close()
		}
	}
	if len(usedPorts) > 0 {
		err := fmt.Errorf("could not listen on port(s) %v - check they are free", usedPorts)
		res.Error = err
		res.Status = StatusCritical
	}
	return res
}
