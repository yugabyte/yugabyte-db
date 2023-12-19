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
	host := config.String(util.NodeBindIpKey)
	port := config.String(util.NodePortKey)
	version := config.String(util.PlatformVersionKey)
	util.FileLogger().Infof(ctx,
		"Starting Node Agent registration (Version: %s)", version)
	addr := fmt.Sprintf("%s:%s", host, port)
	util.FileLogger().Infof(ctx, "Starting RPC server on %s...", addr)
	serverConfig := &RPCServerConfig{
		Address: addr,
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
	customerId := config.String(util.CustomerIdKey)
	if customerId == "" {
		// Populate the customer ID from the API token.
		err := RetrieveUser(ctx, apiToken)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Node Agent Unregistration Failed - %s", err)
			return err
		}
	}
	nodeAgentId := config.String(util.NodeAgentIdKey)
	if nodeAgentId == "" {
		// It is possible to miss node agent ID in the config due to installation cancellation.
		// Try to get it by IP.
		nodeAgentIp := config.String(util.NodeIpKey)
		if nodeAgentIp == "" {
			err := errors.New(
				"Node Agent Unregistration Failed - Node Agent ID and IP not found in the config",
			)
			util.FileLogger().Errorf(ctx, "Node Agent Unregistration Failed - %s", err)
			return err
		}
		util.FileLogger().Infof(ctx, "Getting Node Agent by IP %s", nodeAgentIp)
		getNodeAgentHandler := task.NewGetNodeAgentHandler(apiToken)
		err := executor.GetInstance().ExecuteTask(ctx, getNodeAgentHandler.Handle)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Node Agent Unregistration Failed - %s", err)
			return err
		}
		nodeAgent := getNodeAgentHandler.Result()
		nodeAgentId = nodeAgent.Uuid
		err = config.Update(util.NodeAgentIdKey, nodeAgent.Uuid)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Node Agent Unregistration Failed - %s", err)
			return err
		}
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

// ValidateNodeAgentIfExists validates if the existing node agent communication works.
func ValidateNodeAgentIfExists(ctx context.Context, apiToken string) error {
	config := util.CurrentConfig()
	getNodeAgentHandler := task.NewGetNodeAgentHandler(apiToken)
	err := executor.GetInstance().ExecuteTask(ctx, getNodeAgentHandler.Handle)
	if err == util.ErrNotExist {
		util.FileLogger().Info(ctx, "Node Agent does not exist")
		return err
	}
	if err != nil {
		util.FileLogger().Errorf(ctx, "Node Agent Get failed - %s", err)
		return err
	}
	nodeAgent := getNodeAgentHandler.Result()
	if nodeAgent.State != string(model.Ready) {
		err = fmt.Errorf("Node Agent state %s is not ready", nodeAgent.State)
		util.FileLogger().Errorf(ctx, "Node Agent validation failed - %s", err)
		return err
	}
	nodeAgentId := config.String(util.NodeAgentIdKey)
	if nodeAgentId == "" {
		// Fix the node agent ID in case it was not saved.
		err = config.Update(util.NodeAgentIdKey, nodeAgent.Uuid)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Unable to store node agent ID - %s", err)
			return err
		}
	} else if nodeAgentId != nodeAgent.Uuid {
		err = fmt.Errorf("Node Agent ID %s does not match", nodeAgent.Uuid)
		util.FileLogger().Errorf(ctx, "Node Agent validation failed - %s", err)
		return err
	}
	// Validate the keys by not using the API token.
	getNodeAgentHandler = task.NewGetNodeAgentHandler("")
	err = executor.GetInstance().ExecuteTask(ctx, getNodeAgentHandler.Handle)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Node Agent Get failed - %s", err)
		return err
	}
	return nil
}
