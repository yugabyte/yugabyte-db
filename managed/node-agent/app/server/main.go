// Copyright (c) YugaByte, Inc.

package server

import (
	"context"
	"fmt"
	"node-agent/app/executor"
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
			err := HandleRestart(ctx, config)
			if err == nil {
				ticker.Stop()
				break loop
			}
			util.FileLogger().Errorf("Error handling restart - %s", err.Error())
		case <-sigs:
			cancelFunc()
			return
		}
	}
	addr := fmt.Sprintf("%s:%s", host, port)
	server, err := NewRPCServer(ctx, addr, true)
	if err != nil {
		util.FileLogger().Fatalf("Error in starting RPC server - %s", err.Error())
	}
	util.FileLogger().Infof("Started Service on %s", addr)
	<-sigs
	server.Stop()
	cancelFunc()
	executor.GetInstance(ctx).WaitOnShutdown()
}
