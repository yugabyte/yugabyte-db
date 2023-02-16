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
}

// Entry for all commands.
func main() {
	executor.Init(server.Context())
	defer executor.GetInstance().WaitOnShutdown()
	scheduler.Init(server.Context())
	defer scheduler.GetInstance().WaitOnShutdown()
	task.InitTaskManager(server.Context())
	defer server.CancelFunc()()
	setDefaultConfigs()
	cli.Execute()
}
