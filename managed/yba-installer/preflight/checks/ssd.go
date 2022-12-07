/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var defaultMinSSDStorage float64 = 50

// Ssd is the check for ssd/disk size
var Ssd = &ssdCheck{"ssd", true}

type ssdCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (s ssdCheck) Name() string {
	return s.name
}

// SkipAllowed returns if the skip can be skipped
func (s ssdCheck) SkipAllowed() bool {
	return s.skipAllowed
}

// Execute runs the check. Will validate there is enough disk space
func (s ssdCheck) Execute() Result {
	res := Result{
		Check:  s.name,
		Status: StatusPassed,
	}
	command := "df"
	args := []string{"-H", "--total"}
	output, err := common.ExecuteBashCommand(command, args)
	if err != nil {
		res.Error = err
		res.Status = StatusCritical
		return res
	}

	totalIndex := common.IndexOf(strings.Fields(output), "total")
	sto_str := strings.Split(strings.Fields(output)[totalIndex+1], " ")[0]
	units := string(sto_str[len(sto_str)-1])
	availableSSDstorage, _ := strconv.ParseFloat(sto_str[:len(sto_str)-1], 64)
	if units == "T" {
		availableSSDstorage *= 1024
	}
	if availableSSDstorage < defaultMinSSDStorage {
		res.Error = fmt.Errorf("System does not meet the minimum available SSD storage of %v GB.",
			defaultMinSSDStorage)
		res.Status = StatusCritical
		return res
	}
	log.Info(fmt.Sprintf("System meets the minimum available SSD storage of %v GB.",
		defaultMinSSDStorage))
	return res
}
