// Copyright (c) YugaByte, Inc.

package service

import (
	"context"
	"node-agent/app/executor"
	"node-agent/app/scheduler"
	"node-agent/app/task"
	"node-agent/util"
	"os"
	"os/signal"
	"syscall"
	"time"
)

const serverStartRetryInterval = 10 //in sec
var (
	ctx        context.Context
	cancelFunc context.CancelFunc
	sigs       chan os.Signal
)

func init() {
	ctx, cancelFunc = context.WithCancel(context.Background())
}

func Context() context.Context {
	return ctx
}

func CancelFunc() context.CancelFunc {
	return cancelFunc
}

// Entry method for service.
func Start() {
	sigs = make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM)
	config := util.GetConfig()
	nodeAgentId := config.GetString(util.NodeAgentId)
	if nodeAgentId == "" {
		panic("Node Agent ID must be set")
	}
	//Change the state of node-agent from UPGRADED to LIVE before starting the service.
	for {
		util.FileLogger.Error("Checking the state of the node agent")
		//Keep trying to change the node agent state to LIVE
		if err := task.HandleUpgradedStateAfterRestart(ctx, config); err != nil {
			util.CliLogger.Errorf(
				"Error while handling Node Agent Upgraded State - %s",
				err.Error(),
			)
			time.Sleep(serverStartRetryInterval)
		} else {
			break
		}
	}
	pingStateInterval := time.Duration(config.GetInt(util.NodePingInterval)) * time.Second
	scheduler.GetInstance(ctx).Schedule(ctx, pingStateInterval, task.HandleAgentState(config))
	util.CliLogger.Infof("Started Service")
	<-sigs
	cancelFunc()
	executor.GetInstance(ctx).WaitOnShutdown()
}
