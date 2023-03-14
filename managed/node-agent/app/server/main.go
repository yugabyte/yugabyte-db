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
	ctx = util.WithCorrelationID(context.Background(), util.NewUUID().String())
	ctx, cancelFunc = context.WithCancel(ctx)
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
		util.FileLogger().Fatalf(Context(), "Node Agent ID must be set")
	}
	executor.Init(Context())
	host := config.String(util.NodeIpKey)
	port := config.String(util.NodePortKey)

	// Change the state of node-agent from UPGRADED to LIVE before starting the service.
	ticker := time.NewTicker(serverStartRetryIntervalSec * time.Second)
loop:
	for {
		select {
		case <-ticker.C:
			err := HandleRestart(Context(), config)
			if err == nil {
				ticker.Stop()
				break loop
			}
			util.FileLogger().Errorf(Context(), "Error handling restart - %s", err.Error())
		case <-sigs:
			cancelFunc()
			return
		}
	}
	addr := fmt.Sprintf("%s:%s", host, port)
	server, err := NewRPCServer(Context(), addr, true)
	if err != nil {
		util.FileLogger().Fatalf(Context(), "Error in starting RPC server - %s", err.Error())
	}
	util.FileLogger().Infof(Context(), "Started Service on %s", addr)
	<-sigs
	server.Stop()
}
