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

var Ssd = &ssdCheck{"ssd", "warning"}

type ssdCheck struct {
	name         string
	warningLevel string
}

func (s ssdCheck) Name() string {
	return s.name
}

func (s ssdCheck) WarningLevel() string {
	return s.warningLevel
}

func (s ssdCheck) Execute() error {

	command := "df"
	args := []string{"-H", "--total"}
	output, err := common.ExecuteBashCommand(command, args)
	if err != nil {
		return err
	}

	totalIndex := common.IndexOf(strings.Fields(output), "total")
	sto_str := strings.Split(strings.Fields(output)[totalIndex+1], " ")[0]
	units := string(sto_str[len(sto_str)-1])
	availableSSDstorage, _ := strconv.ParseFloat(sto_str[:len(sto_str)-1], 64)
	if units == "T" {
		availableSSDstorage *= 1024
	}
	if availableSSDstorage < defaultMinSSDStorage {
		return fmt.Errorf("System does not meet the minimum available SSD storage of %v GB.",
			defaultMinSSDStorage)
	}
	log.Info(fmt.Sprintf("System meets the minimum available SSD storage of %v GB.",
		defaultMinSSDStorage))
	return nil
}
