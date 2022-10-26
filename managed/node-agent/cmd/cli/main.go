// Copyright (c) YugaByte, Inc.

package main

import (
	"node-agent/cli"
	"node-agent/util"
)

func setDefaultConfigs() {
	config := util.CurrentConfig()
	_, err := config.CompareAndUpdate(util.NodeLoggerKey, "", util.NodeAgentDefaultLog)
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodePortKey, "", util.NodePort)
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.PlatformVersionKey, "", util.Version())
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.RequestTimeoutKey, "", "20")
	if err != nil {
		panic(err)
	}
	_, err = config.CompareAndUpdate(util.NodePingIntervalKey, "", "20")
	if err != nil {
		panic(err)
	}
}

// Entry for CLI.
func main() {
	setDefaultConfigs()
	cli.Execute()
}
