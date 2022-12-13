// Copyright (c) YugaByte, Inc.

package main

import (
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
	_, err = config.CompareAndUpdate(util.PlatformVersionKey, nil, util.Version())
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.RequestTimeoutKey, nil, "20")
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodePingIntervalKey, nil, "20")
	if err != nil {
		panic(err)
	}
}

// Entry for CLI.
func main() {
	setDefaultConfigs()
	cli.Execute()
}
