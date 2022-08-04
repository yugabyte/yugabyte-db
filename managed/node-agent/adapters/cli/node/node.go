// Copyright (c) YugaByte, Inc.
package node

import (
	"fmt"
	"node-agent/app/executor"
	"node-agent/app/service"
	"node-agent/util"

	"github.com/spf13/cobra"
)

var (
	nodeCmd = &cobra.Command{
		Use:   "node ...",
		Short: "Command for node agent",
	}
	appConfig    *util.Config
	taskExecutor *executor.TaskExecutor
)

func SetupNodeCommand(parentCmd *cobra.Command) {
	appConfig = util.GetConfig()
	taskExecutor = executor.GetInstance(service.Context())
	SetupConfigureCommand(nodeCmd)
	SetupPreflightCheckCommand(nodeCmd)
	SetupRegisterCommand(nodeCmd)
	parentCmd.AddCommand(nodeCmd)
}

// Accepts a key and desciption and updates the config with user input.
func checkConfigAndUpdate(key string, desc string) {
	val := appConfig.GetString(key)

	//Display the current set config
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

	if newVal != "" {
		appConfig.Update(key, newVal)
	}
}
