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

var DiskSize = &diskSizeCheck{"disk size", true}

type diskSizeCheck struct {
	name        string
	skipAllowed bool
}

func (s diskSizeCheck) Name() string {
	return s.name
}

func (s diskSizeCheck) SkipAllowed() bool {
	return s.skipAllowed
}

func (s diskSizeCheck) Execute() Result {
	res := Result{
		Check:  s.name,
		Status: StatusPassed,
	}
	command := "df"
	args := []string{"-H", "--total"}
	output, err := common.RunBash(command, args)
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
