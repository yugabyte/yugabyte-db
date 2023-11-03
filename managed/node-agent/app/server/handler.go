// Copyright (c) YugaByte, Inc.

package server

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/executor"
	"node-agent/app/task"
	"node-agent/model"
	"node-agent/util"
	"strings"
)

// Retrives current user associated with the API token from the platform.
func RetrieveUser(ctx context.Context, apiToken string) error {
	config := util.CurrentConfig()
	sessionInfoHandler := task.NewGetSessionInfoHandler(apiToken)
	err := executor.GetInstance().
		ExecuteTask(ctx, sessionInfoHandler.Handle)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error fetching the session info - %s", err)
		return err
	}
	sessionInfo := sessionInfoHandler.Result()
	config.Update(util.CustomerIdKey, sessionInfo.CustomerId)
	config.Update(util.UserIdKey, sessionInfo.UserId)

	userHandler := task.NewGetUserHandler(apiToken)
	err = executor.GetInstance().
		ExecuteTask(ctx, userHandler.Handle)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error fetching the user %s - %s", sessionInfo.UserId, err)
		return err
	}
	user := userHandler.Result()
	if strings.EqualFold(user.Role, "ReadOnly") {
		err = fmt.Errorf("User must have SuperAdmin role instead of %s", user.Role)
		util.FileLogger().Errorf(ctx, "Unsupported user role - %s", err.Error())
		return err
	}
	return nil
}

// Registers the node agent to the platform.
func RegisterNodeAgent(ctx context.Context, apiToken string) error {
	config := util.CurrentConfig()
	host := config.String(util.NodeIpKey)
	port := config.String(util.NodePortKey)
	version := config.String(util.PlatformVersionKey)
	util.FileLogger().Infof(ctx,
		"Starting Node Agent registration (Version: %s)", version)
	util.FileLogger().Info(ctx, "Starting RPC server...")
	serverConfig := &RPCServerConfig{
		Address: fmt.Sprintf("%s:%s", host, port),
	}
	// Start server to verify host.
	server, err := NewRPCServer(ctx, serverConfig)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Failed to start RPC server - %s", err.Error())
		return err
	}
	defer server.Stop()
	util.FileLogger().Info(ctx, "Submiting Registration task to the executor.")
	registrationHandler := task.NewAgentRegistrationHandler(apiToken)
	// Call platform to register the node agent.
	err = executor.GetInstance().
		ExecuteTask(ctx, registrationHandler.Handle)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Node Agent Registration Failed - %s", err)
		return err
	}
	data := registrationHandler.Result()
	nuuid := data.Uuid
	config.Update(util.NodeAgentIdKey, nuuid)
	util.FileLogger().Info(ctx, "Saving the node agent certs.")
	certsUUID := util.NewUUID().String()
	config.Update(util.PlatformCertsKey, certsUUID)
	err = util.SaveCerts(ctx, config, data.Config.ServerCert, data.Config.ServerKey, certsUUID)
	if err != nil {
		util.FileLogger().Info(
			ctx,
			"Error while saving certs, unregistering the node-agent in the platform.",
		)
		UnregisterNodeAgent(ctx, apiToken)
		return err
	}
	util.FileLogger().Info(ctx, "Setting node agent state to Ready.")
	agentStateHandler := task.NewPutAgentStateHandler(model.Ready, version)
	// TODO add generic retry for HTTP calls.
	err = executor.GetInstance().ExecuteTask(
		ctx,
		agentStateHandler.Handle,
	)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Node Agent Registration Failed - %s", err)
		return err
	}
	util.FileLogger().Infof(ctx, "Node Agent Registration Successful with Node ID - %s.", nuuid)
	return nil
}

// Unregisters the node agent from the platform.
func UnregisterNodeAgent(ctx context.Context, apiToken string) error {
	config := util.CurrentConfig()
	nodeAgentId := config.String(util.NodeAgentIdKey)
	// Return error if there is no node agent id present in the config.
	if nodeAgentId == "" {
		err := errors.New(
			"Node Agent Unregistration Failed - Node Agent ID not found in the config",
		)
		util.FileLogger().Error(ctx, err.Error())
		return err
	}
	util.FileLogger().Infof(ctx, "Unregistering Node Agent - %s", nodeAgentId)
	unregisterHandler := task.NewAgentUnregistrationHandler(apiToken)
	err := executor.GetInstance().
		ExecuteTask(ctx, unregisterHandler.Handle)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Node Agent Unregistration Failed - %s", err)
		return err
	}
	config.Remove(util.NodeAgentIdKey)
	util.FileLogger().Infof(ctx, "Node Agent Unregistration Successful")
	return nil
}
