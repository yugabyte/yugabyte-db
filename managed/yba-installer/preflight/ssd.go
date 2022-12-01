/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var ssd = Ssd{"ssd", "warning"}

type Ssd struct {
	name         string
	warningLevel string
}

func (s Ssd) Name() string {
	return s.name
}

func (s Ssd) WarningLevel() string {
	return s.warningLevel
}

func (s Ssd) Execute() {

	command := "df"
	args := []string{"-H", "--total"}
	output, err := common.ExecuteBashCommand(command, args)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		totalIndex := common.IndexOf(strings.Fields(output), "total")
		sto_str := strings.Split(strings.Fields(output)[totalIndex+1], " ")[0]
		units := string(sto_str[len(sto_str)-1])
		availableSSDstorage, _ := strconv.ParseFloat(sto_str[:len(sto_str)-1], 64)
		if units == "T" {
			availableSSDstorage *= 1024
		}
		if availableSSDstorage < defaultMinSSDStorage {
			log.Fatal(
				fmt.Sprintf("System does not meet the minimum available SSD storage of %v GB.",
					defaultMinSSDStorage))
		} else {
			log.Info(
				fmt.Sprintf("System meets the minimum available SSD storage of %v GB.",
					defaultMinSSDStorage))
		}
	}
}

func init() {
	RegisterPreflightCheck(ssd)
}
