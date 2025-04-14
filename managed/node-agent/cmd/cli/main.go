// Copyright (c) YugaByte, Inc.

package main

import (
	"node-agent/app/executor"
	"node-agent/app/scheduler"
	"node-agent/app/server"
	"node-agent/app/task"
	"node-agent/cli"
	"node-agent/util"
)

func setDefaultConfigs() {
	config := util.CurrentConfig()
	_, err := config.CompareAndUpdate(util.NodeLoggerKey, nil, util.NodeAgentDefaultLog)
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodePortKey, nil, util.NodePort)
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.PlatformVersionKey, nil, util.MustVersion())
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.RequestTimeoutKey, nil, "20")
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodeAgentLogLevelKey, nil, "1" /* Info */)
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodeAgentLogMaxMbKey, nil, "100")
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodeAgentLogMaxBackupsKey, nil, "5")
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodeAgentLogMaxDaysKey, nil, "15")
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodeAgentDisableMetricsTLS, nil, "true")
	if err != nil {
		panic(err)
	}
	nodeIp := config.String(util.NodeIpKey)
	if nodeIp != "" {
		// Populate bind IP if it is not already populated.
		_, err = config.CompareAndUpdate(util.NodeBindIpKey, nil, nodeIp)
		if err != nil {
			panic(err)
		}
	}
}

// Entry for all commands.
func main() {
	defer func() {
		if r := recover(); r != nil {
			util.ConsoleLogger().
				Fatalf(server.Context(), "Panic occurred: %v. Make sure node agent is correctly "+
					"installed and the right user is running the command.", r)
		}
	}()
	executor.Init(server.Context())
	defer executor.GetInstance().WaitOnShutdown()
	scheduler.Init(server.Context())
	defer scheduler.GetInstance().WaitOnShutdown()
	task.InitTaskManager(server.Context())
	defer server.CancelFunc()()
	setDefaultConfigs()
	cli.Execute()
}
