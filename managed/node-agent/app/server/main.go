// Copyright (c) YugaByte, Inc.

package server

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

const serverStartRetryIntervalSec = 10 //in sec
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
	config := util.CurrentConfig()
	nodeAgentId := config.String(util.NodeAgentIdKey)
	if nodeAgentId == "" {
		util.FileLogger().Fatalf("Node Agent ID must be set")
	}
	host := config.String(util.NodeIpKey)
	port := config.String(util.NodePortKey)

	// Change the state of node-agent from UPGRADED to LIVE before starting the service.
	ticker := time.NewTicker(serverStartRetryIntervalSec * time.Second)
loop:
	for {
		select {
		case <-ticker.C:
			err := task.HandleUpgradedStateAfterRestart(ctx, config)
			if err == nil {
				ticker.Stop()
				break loop
			}
			util.FileLogger().Errorf("Error while handling node agent UPGRADED state - %s", err.Error())
		case <-sigs:
			cancelFunc()
			return
		}
	}
	server, err := NewRPCServer(ctx, host, port, true)
	if err != nil {
		util.FileLogger().Fatalf("Error in starting RPC server - %s", err.Error())
	}
	pingStateInterval := time.Duration(config.Int(util.NodePingIntervalKey)) * time.Second
	scheduler.GetInstance(ctx).Schedule(ctx, pingStateInterval, task.HandleAgentState(config))
	util.ConsoleLogger().Infof("Started Service")
	<-sigs
	server.Stop()
	cancelFunc()
	executor.GetInstance(ctx).WaitOnShutdown()
}
