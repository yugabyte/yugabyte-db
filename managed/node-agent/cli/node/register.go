// Copyright (c) YugaByte, Inc.

package node

import (
	"errors"
	"node-agent/app/server"
	"node-agent/util"

	"github.com/spf13/cobra"
)

var (
	registerCmd = &cobra.Command{
		Use:   "register",
		Short: "Registers a node",
		Long:  "Registers a node with the Platform by making a call to the platform.",
		Run:   registerCmdHandler,
	}

	unregisterCmd = &cobra.Command{
		Use:   "unregister",
		Short: "Unregisters a node",
		RunE:  unregisterCmdHandler,
	}
)

func SetupRegisterCommand(parentCmd *cobra.Command) {
	registerCmd.PersistentFlags().
		StringP("api_token", "t", "", "API Token for registering the node.")
	registerCmd.PersistentFlags().StringP("node_ip", "n", "", "Node IP")
	registerCmd.PersistentFlags().StringP("url", "u", "", "Platform URL")
	registerCmd.MarkPersistentFlagRequired("api_token")
	parentCmd.AddCommand(registerCmd)
	parentCmd.AddCommand(unregisterCmd)
}

func unregisterCmdHandler(cmd *cobra.Command, args []string) error {
	// Run the unregister flow using JWT.
	return unregisterHandler(true, "")
}

func unregisterHandler(useJWT bool, apiToken string) error {
	nodeAgentId := util.CurrentConfig().String(util.NodeAgentIdKey)
	// Return error if there is no node agent id present in the config.
	if nodeAgentId == "" {
		err := errors.New(
			"Node Agent Unregistration Failed - Node Agent ID not found in the config",
		)
		util.ConsoleLogger().Errorf(err.Error())
		return err
	}

	util.ConsoleLogger().Infof("Unregistering Node Agent - %s", nodeAgentId)
	err := server.UnregisterNodeAgent(server.Context(), apiToken, useJWT)
	if err != nil {
		util.ConsoleLogger().Errorf("Node Agent Unregistration Failed - %s", err)
		return err
	}
	util.ConsoleLogger().Infof("Node Agent Unregistration Successful")
	return nil
}

func registerCmdHandler(cmd *cobra.Command, args []string) {
	config := util.CurrentConfig()
	apiToken, err := cmd.Flags().GetString("api_token")
	if err != nil {
		util.ConsoleLogger().Fatalf("Unable to get API token - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		cmd,
		"node_ip",
		util.NodeIpKey,
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf("Unable to store node IP - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		cmd,
		"url",
		util.PlatformUrlKey,
		true, /* isRequired */
		util.ExtractBaseURL,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf("Unable to get platform URL - %s", err.Error())
	}
	err = server.RetrieveUser(apiToken)
	if err != nil {
		util.ConsoleLogger().Fatalf("Error fetching the current user with the API key - %s", err)
	}
	err = server.RegisterNodeAgent(server.Context(), apiToken)
	if err != nil {
		util.ConsoleLogger().Fatalf("Unable to register node agent - %s", err.Error())
	}
	util.ConsoleLogger().Info("Node Agent Registration Successful")
}
