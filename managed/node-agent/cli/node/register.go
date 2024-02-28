// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"net"
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
		StringP("api_token", "t", "", "API token for registering the node.")
	registerCmd.PersistentFlags().StringP("node_ip", "n", "", "Node IP")
	registerCmd.PersistentFlags().StringP("bind_ip", "b", "", "Bind IP")
	registerCmd.PersistentFlags().StringP("node_port", "p", "", "Node Port")
	registerCmd.PersistentFlags().StringP("url", "u", "", "Platform URL")
	registerCmd.PersistentFlags().Bool("skip_verify_cert", false,
		"Skip Yugabyte Anywhere SSL cert verification.")
	registerCmd.MarkPersistentFlagRequired("api_token")
	unregisterCmd.PersistentFlags().
		StringP("api_token", "t", "", "Optional API token for unregistering the node.")
	unregisterCmd.PersistentFlags().StringP("node_id", "i", "", "Node ID")
	unregisterCmd.PersistentFlags().StringP("node_ip", "n", "", "Node IP")
	unregisterCmd.PersistentFlags().StringP("url", "u", "", "Platform URL")
	unregisterCmd.PersistentFlags().Bool("skip_verify_cert", false,
		"Skip Yugabyte Anywhere SSL cert verification.")
	parentCmd.AddCommand(registerCmd)
	parentCmd.AddCommand(unregisterCmd)
}

func unregisterCmdHandler(cmd *cobra.Command, args []string) error {
	ctx := server.Context()
	config := util.CurrentConfig()
	apiToken, _ := cmd.Flags().GetString("api_token")
	_, err := config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_id",
		util.NodeAgentIdKey,
		nil,   /* default value */
		false, /* isRequired */
		nil,   /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent ID - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_ip",
		util.NodeIpKey,
		nil,  /* default value */
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node IP - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"url",
		util.PlatformUrlKey,
		nil,  /* default value */
		true, /* isRequired */
		util.ExtractBaseURL,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store platform URL - %s", err.Error())
	}
	_, err = config.StoreCommandFlagBool(
		ctx,
		cmd,
		"skip_verify_cert",
		util.PlatformSkipVerifyCertKey,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error storing skip_verify_cert value - %s", err.Error())
	}
	// API token is optional.
	return unregisterHandler(ctx, apiToken)
}

func unregisterHandler(ctx context.Context, apiToken string) error {
	config := util.CurrentConfig()
	nodeAgentIp := config.String(util.NodeIpKey)
	util.ConsoleLogger().Infof(ctx, "Unregistering Node Agent with IP %s", nodeAgentIp)
	err := server.UnregisterNodeAgent(server.Context(), apiToken)
	if err != nil {
		util.ConsoleLogger().Errorf(ctx, "Node Agent Unregistration Failed - %s", err)
		return err
	}
	util.ConsoleLogger().Infof(ctx, "Node Agent Unregistration Successful")
	return nil
}

func registerCmdHandler(cmd *cobra.Command, args []string) {
	ctx := server.Context()
	config := util.CurrentConfig()
	apiToken, err := cmd.Flags().GetString("api_token")
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to get API token - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_ip",
		util.NodeIpKey,
		nil,  /* default value */
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node IP - %s", err.Error())
	}
	nodeIp := config.String(util.NodeIpKey)
	parsedIp := net.ParseIP(nodeIp)
	if parsedIp == nil {
		// Get the bind IP if it is DNS. It defaults to the DNS if it is not present.
		_, err = config.StoreCommandFlagString(
			ctx,
			cmd,
			"bind_ip",
			util.NodeBindIpKey,
			&nodeIp,
			true, /* isRequired */
			nil,  /* validator */
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to store bind IP - %s", err.Error())
		}
	} else {
		// Use the node IP as the bind IP.
		err = config.Update(util.NodeBindIpKey, nodeIp)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent bind IP - %s", err.Error())
		}
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_port",
		util.NodePortKey,
		nil,   /* default value */
		false, /* isRequired */
		nil,   /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node port - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"url",
		util.PlatformUrlKey,
		nil,  /* default value */
		true, /* isRequired */
		util.ExtractBaseURL,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store platform URL - %s", err.Error())
	}
	_, err = config.StoreCommandFlagBool(
		ctx,
		cmd,
		"skip_verify_cert",
		util.PlatformSkipVerifyCertKey,
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store skip_verify_cert value - %s", err.Error())
	}
	err = server.RetrieveUser(ctx, apiToken)
	if err != nil {
		util.ConsoleLogger().
			Fatalf(ctx, "Error fetching the current user with the API key - %s", err)
	}

	err = server.RegisterNodeAgent(server.Context(), apiToken)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to register node agent - %s", err.Error())
	}
	util.ConsoleLogger().Info(ctx, "Node Agent Registration Successful")
}
