// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/task"
	"node-agent/model"
	"node-agent/util"

	"github.com/spf13/cobra"
)

var (
	registerCmd = &cobra.Command{
		Use:   "register",
		Short: "Registers a node",
		Long:  "Registers a node with the Platform by making a call to the platform.",
		RunE:  registerCmdHandler,
	}

	unregisterCmd = &cobra.Command{
		Use:   "unregister",
		Short: "Unregisters a node",
		RunE:  unregisterCmdHandler,
	}
)

func SetupRegisterCommand(parentCmd *cobra.Command) {
	registerCmd.PersistentFlags().String("api_token", "", "API Token for registering the node.")
	registerCmd.MarkPersistentFlagRequired("api_token")
	parentCmd.AddCommand(registerCmd)
	parentCmd.AddCommand(unregisterCmd)
}

func unregisterCmdHandler(cmd *cobra.Command, args []string) error {
	//Run the unregister flow using JWT.
	return unregisterHandler(true, "")
}

func unregisterHandler(useJWT bool, apiToken string) error {

	ctx := context.Background()
	nodeAgentId := appConfig.GetString(util.NodeAgentId)

	//Return error if there is no node agent id present in the config.
	if nodeAgentId == "" {
		err := errors.New(
			"Node Agent Unregistration Failed - Node Agent ID not found in the config",
		)
		util.CliLogger.Errorf(err.Error())
		return err
	}
	util.CliLogger.Infof("Unregistering Node Agent - %s", nodeAgentId)

	_, err := taskExecutor.ExecuteTask(ctx, task.HandleAgentUnregister(useJWT, apiToken))
	if err != nil {
		util.CliLogger.Errorf("Node Agent Unregistration Failed - %s", err)
		return err
	}
	appConfig.Update(util.NodeAgentId, "")
	util.CliLogger.Infof("Node Agent Unregistration Successful")
	return nil
}

func registerCmdHandler(cmd *cobra.Command, args []string) error {
	apiToken, err := cmd.Flags().GetString("api_token")
	if err != nil {
		util.CliLogger.Errorf("Unable to get API token - %s", err.Error())
	}
	ctx := context.Background()

	util.CliLogger.Infof(
		"Starting Node Agent registration (Version: %s)",
		appConfig.GetString(util.PlatformVersion),
	)
	util.CliLogger.Info("Submiting Registration task to the executor.")
	//Call the Platform to register the node agent.
	response, err := taskExecutor.ExecuteTask(ctx, task.HandleAgentRegistration(apiToken))
	if err != nil {
		util.CliLogger.Errorf("Node Agent Registration Failed - %s", err)
		return err
	}
	data, ok := response.(*model.RegisterResponseSuccess)
	if !ok {
		err = fmt.Errorf("Node Agent Registering Failed - Type Inference Error")
		util.CliLogger.Error(err.Error())
		return err
	}

	nuuid := data.Uuid
	//Update the config with the node uuid.
	appConfig.Update(util.NodeAgentId, nuuid)
	//Save the certs in the node.
	util.CliLogger.Info("Saving the node agent certs.")

	certsUUID := util.NewUUID().String()
	appConfig.Update(util.PlatformCerts, certsUUID)
	err = util.SaveCerts(appConfig, data.Config.ServerCert, data.Config.ServerKey, certsUUID)
	if err != nil {
		util.CliLogger.Info(
			"Error while saving certs, unregistering the node-agent in the platform.",
		)
		//Use API token to run the unregistration to avoid using corrupt JWT.
		unregisterHandler(false, apiToken)
		return err
	}
	util.CliLogger.Info("Setting node agent state to LIVE.")
	// TODO add generic retry for HTTP calls.
	response, err = taskExecutor.ExecuteTask(
		ctx,
		task.HandlePutAgentState(model.Live, appConfig.GetString(util.PlatformVersion)),
	)
	if err != nil {
		util.CliLogger.Errorf("Node Agent Registration Failed - %s", err)
		return err
	}

	util.CliLogger.Infof("Node Agent Registration Successful with Node ID - %s.", nuuid)
	return nil
}
