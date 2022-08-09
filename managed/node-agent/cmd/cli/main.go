// Copyright (c) YugaByte, Inc.

package main

import (
	"fmt"
	"node-agent/adapters/cli"
	"node-agent/app/task"
	"node-agent/util"
)

// Entry for CLI
func main() {
	err := util.InitConfig(util.DefaultConfig)
	if err != nil {
		fmt.Printf("Error while initializing the config %s", err.Error())
		return
	}
	config := util.GetConfig()
	config.Update(util.NodeLogger, util.NodeAgentDefaultLog)
	util.InitCommonLoggers()
	util.FileLogger.Info("Starting yb node agent app")
	task.InitHttpClient(util.GetConfig())
	cli.Execute()
}
