// Copyright (c) YugaByte, Inc.

package node

import (
	"fmt"
	"node-agent/util"
	"strings"

	"github.com/spf13/cobra"
)

var (
	nodeCmd = &cobra.Command{
		Use:   "node ...",
		Short: "Command for node agent",
	}
)

func SetupNodeCommand(parentCmd *cobra.Command) {
	SetupConfigureCommand(nodeCmd)
	SetupPreflightCheckCommand(nodeCmd)
	SetupRegisterCommand(nodeCmd)
	parentCmd.AddCommand(nodeCmd)
}

// Accepts a key and desciption and updates the config with user input.
func checkConfigAndUpdate(key string, desc string) {
	config := util.CurrentConfig()
	for {
		val := config.String(key)
		if val != "" {
			fmt.Printf(
				"* The current value of %s is set to %s; Enter new value or enter to skip: ",
				desc,
				val,
			)
		} else {
			fmt.Printf("* The current value of %s is not set; Enter new value or enter to skip: ", desc)
		}
		var newVal string
		fmt.Scanln(&newVal)
		newVal = strings.TrimSpace(newVal)
		if newVal == "" {
			if val != "" {
				break
			}
		} else {
			err := config.Update(key, newVal)
			if err != nil {
				util.ConsoleLogger().Errorf("Error in updating value %s for key %s", newVal, key)
				fmt.Println()
				continue
			}
			break
		}
	}
}
