// Copyright (c) YugabyteDB, Inc.

package server

import (
	"context"
	"errors"
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
	// Held for the life of the process; the lock is released if this file is closed or collected.
	serverLock *os.File
)

func init() {
	ctx = context.WithValue(context.Background(), util.CorrelationId, util.NewUUID().String())
	ctx, cancelFunc = context.WithCancel(ctx)
}

func Context() context.Context {
	return ctx
}

func CancelFunc() context.CancelFunc {
	return cancelFunc
}

// acquireServerLock makes sure only one server runs per install. A second one (e.g. a leftover
// system-level unit next to the user-level one) would hold a config loaded before an upgrade and
// write it back over the upgraded one (PLAT-24106). The second one waits instead of exiting, as
// its unit restarts it forever and each exit would log again. Returns false on a stop signal.
func acquireServerLock() bool {
	lockPath := util.ServerLockPath()
	lock, err := util.TryLockFile(lockPath)
	if err == nil {
		serverLock = lock
		return true
	}
	if !errors.Is(err, util.ErrLockHeld) {
		util.FileLogger().Fatalf(Context(), "Error locking %s - %s", lockPath, err.Error())
	}
	util.FileLogger().Warnf(Context(),
		"Another node agent server is running for %s (%s). Waiting for it to stop. "+
			"Remove duplicate yb-node-agent systemd units.",
		util.MustGetHomeDirectory(), err.Error())
	ticker := time.NewTicker(serverStartRetryIntervalSec * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ticker.C:
			lock, err = util.TryLockFile(lockPath)
			if err == nil {
				// This process may have been waiting since before an upgrade replaced the binary.
				// Exit so that the unit restarts on the current binary.
				lock.Close()
				util.FileLogger().Fatalf(Context(),
					"The other node agent server stopped. Exiting to restart on the current binary.")
			}
		case <-sigs:
			return false
		}
	}
}

// Entry method for service.
func Start() {
	defer cancelFunc()
	sigs = make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM)
	if !acquireServerLock() {
		return
	}
	config := util.CurrentConfig()
	// The config was loaded before the lock was taken, so it can be older than the file.
	if err := config.Reload(); err != nil {
		util.FileLogger().Fatalf(Context(), "Error reloading config - %s", err.Error())
	}
	nodeAgentId := config.String(util.NodeAgentIdKey)
	if nodeAgentId == "" {
		util.FileLogger().Fatalf(Context(), "Node Agent ID must be set")
	}
	dump, _ := config.Dump(Context())
	util.FileLogger().Infof(Context(), "Dumping config %s", dump)
	executor.Init(Context())
	disableMetricsTLS := config.Bool(util.NodeAgentDisableMetricsTLSKey)
	host := config.String(util.NodeBindIpKey)
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
			return
		}
	}
	monitorDuplicateServerUnits(Context())
	addr := fmt.Sprintf("%s:%s", host, port)
	serverConfig := &RPCServerConfig{
		Address:           addr,
		EnableTLS:         true,
		EnableMetrics:     true,
		DisableMetricsTLS: disableMetricsTLS,
	}
	server, err := NewRPCServer(Context(), serverConfig)
	if err != nil {
		util.FileLogger().Fatalf(Context(), "Error in starting RPC server - %s", err.Error())
	}
	util.FileLogger().Infof(Context(), "Started RPC service on %s", addr)
	select {
	case <-sigs:
	case <-server.Done():
	case <-Context().Done():
	}
	server.Stop()
}
