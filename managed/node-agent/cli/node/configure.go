// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"fmt"
	"node-agent/app/executor"
	"node-agent/app/server"
	"node-agent/app/task"
	"node-agent/model"
	"node-agent/util"
	"reflect"
	"strconv"
	"strings"

	"github.com/spf13/cobra"
)

var (
	configureCmd = &cobra.Command{
		Use:    "configure",
		Short:  "Configures a node",
		PreRun: configurePreValidator,
		Run:    configureNodeHandler,
	}
)

func SetupConfigureCommand(parentCmd *cobra.Command) {
	configureCmd.PersistentFlags().
		StringP("api_token", "t", "", "API token for fetching config info.")
	configureCmd.PersistentFlags().StringP("url", "u", "", "Platform URL")
	configureCmd.PersistentFlags().
		Bool("skip_verify_cert", false, "Skip Yugabyte Anywhere SSL cert verification.")
	configureCmd.PersistentFlags().
		Bool("disable_egress", false, "Disable connection from node agent.")
	/* Required only if egress is disabled. */
	configureCmd.PersistentFlags().StringP("id", "i", "", "Node agent ID")
	configureCmd.PersistentFlags().StringP("customer_id", "c", "", "Customer ID")
	configureCmd.PersistentFlags().StringP("cert_dir", "d", "", "Node agent cert directory")
	configureCmd.PersistentFlags().StringP("node_ip", "n", "", "Node IP")
	configureCmd.PersistentFlags().StringP("node_port", "p", "", "Node Port")
	/* End of non-plex flags. */
	parentCmd.AddCommand(configureCmd)
}

func configurePreValidator(cmd *cobra.Command, args []string) {
	if disabled, err := cmd.Flags().GetBool("disable_egress"); err != nil {
		util.ConsoleLogger().
			Fatalf(server.Context(), "Error in reading disable_egress - %s", err.Error())
	} else if disabled {
		cmd.MarkPersistentFlagRequired("id")
		cmd.MarkPersistentFlagRequired("customer_id")
		cmd.MarkPersistentFlagRequired("cert_dir")
		cmd.MarkPersistentFlagRequired("node_ip")
		cmd.MarkPersistentFlagRequired("node_port")
	} else {
		cmd.MarkPersistentFlagRequired("api_token")
		cmd.MarkPersistentFlagRequired("url")
	}
}

func configureNodeHandler(cmd *cobra.Command, args []string) {
	ctx := server.Context()
	if disabled, err := cmd.Flags().GetBool("disable_egress"); err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error in reading disable_egress - %s", err.Error())
	} else if disabled {
		configureDisabledEgress(ctx, cmd)
	} else {
		interactiveConfigHandler(ctx, cmd)
	}
}

func configureDisabledEgress(ctx context.Context, cmd *cobra.Command) {
	config := util.CurrentConfig()
	_, err := config.StoreCommandFlagString(
		ctx,
		cmd,
		"id",
		util.NodeAgentIdKey,
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent ID - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"customer_id",
		util.CustomerIdKey,
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store customer ID - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"cert_dir",
		util.PlatformCertsKey,
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent cert dir - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_ip",
		util.NodeIpKey,
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent IP - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_port",
		util.NodePortKey,
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent port - %s", err.Error())
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
}

// Provides a fully interactive configuration setup for the user to configure
// the node agent. It uses api token to fetch customers and finds
// subsequent properties using the customer ID.
func interactiveConfigHandler(ctx context.Context, cmd *cobra.Command) {
	apiToken, err := cmd.Flags().GetString("api_token")
	if err != nil {
		util.ConsoleLogger().
			Fatalf(ctx, "Need API Token during interactive config setup - %s", err.Error())
	}
	config := util.CurrentConfig()
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"url",
		util.PlatformUrlKey,
		true, /* isRequired */
		util.ExtractBaseURL,
	)
	if err != nil {
		util.ConsoleLogger().
			Fatalf(ctx, "Need Platform URL during interactive config setup - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_port",
		util.NodePortKey,
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent port - %s", err.Error())
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
	// Get node agent name and IP.
	checkConfigAndUpdate(ctx, util.NodeIpKey, "Node IP")
	checkConfigAndUpdate(ctx, util.NodeNameKey, "Node Name")

	err = server.RetrieveUser(ctx, apiToken)
	if err != nil {
		util.ConsoleLogger().Fatalf(
			ctx,
			"Error fetching the current user with the API key - %s", err.Error())
	}
	providersHandler := task.NewGetProvidersHandler(apiToken)
	// Get Providers from the platform (only on-prem providers displayed)
	err = executor.GetInstance().ExecuteTask(ctx, providersHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error fetching the providers - %s", err)
	}
	providers := *providersHandler.Result()
	i := 0
	for _, data := range providers {
		if data.Code == "onprem" {
			providers[i] = data
			i++
		}
	}
	onpremProviders := providers[:i]
	providerNum, err := displayOptionsAndUpdateSelected(
		ctx,
		util.ProviderIdKey,
		displayInterfaces(ctx, onpremProviders),
		"Onprem Provider",
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error while displaying providers - %s", err.Error())
	}
	selectedProvider := onpremProviders[providerNum]

	instanceTypesHandler := task.NewGetInstanceTypesHandler(apiToken)
	// Get Instance Types for the provider from the platform.
	err = executor.GetInstance().
		ExecuteTask(ctx, instanceTypesHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error fetching the instance types - %s", err.Error())
	}
	instances := *instanceTypesHandler.Result()
	_, err = displayOptionsAndUpdateSelected(
		ctx,
		util.NodeInstanceTypeKey,
		displayInterfaces(ctx, instances),
		"Instance Type",
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error while displaying instance Types - %s", err.Error())
	}
	regions := selectedProvider.Regions
	regionNum, err := displayOptionsAndUpdateSelected(
		ctx,
		util.NodeRegionKey, displayInterfaces(ctx, regions), "Region")
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error while displaying regions - %s", err.Error())
	}

	// Update availability Zone.
	zones := regions[regionNum].Zones
	zoneNum, err := displayOptionsAndUpdateSelected(
		ctx,
		util.NodeZoneKey,
		displayInterfaces(ctx, zones),
		"Zone",
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error while displaying zones - %s", err.Error())
	}
	config.Update(util.NodeAzIdKey, zones[zoneNum].Uuid)
	util.ConsoleLogger().Infof(ctx, "Completed Node Agent Configuration")

	err = server.RegisterNodeAgent(server.Context(), apiToken)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to register node agent - %s", err.Error())
	}
	util.ConsoleLogger().Info(ctx, "Node Agent Registration Successful")
}

// Displays the options and prompts the user to select an option followed by validating the option.
func displayOptionsAndUpdateSelected(
	ctx context.Context,
	key string,
	options []model.DisplayInterface,
	displayHead string,
) (int, error) {
	if len(options) == 0 {
		return -1, fmt.Errorf("No record found for %s", displayHead)
	}
	config := util.CurrentConfig()
	selectedIdx := -1
	fmt.Printf("* Select your %s.\n", displayHead)
	for i, option := range options {
		fmt.Printf("%d. %s\n", i+1, option)
	}
	// Check if the key is already set.
	val := config.String(key)
	if val != "" {
		for i, option := range options {
			if option.Id() == val {
				fmt.Printf(
					"* The current value is %s.\n",
					option,
				)
				selectedIdx = i
				break
			}
		}

	}
	for {
		if selectedIdx >= 0 {
			fmt.Printf("\t Enter new option number or enter to skip: ")
		} else {
			fmt.Printf("\t Enter option number: ")
		}
		var newVal string
		fmt.Scanln(&newVal)
		newVal = strings.TrimSpace(newVal)
		if newVal == "" {
			if selectedIdx >= 0 {
				break
			}
			fmt.Println()
			// Continue as long as newVal is not set.
		} else {
			optionNum, err := strconv.Atoi(newVal)
			if err != nil {
				util.ConsoleLogger().Errorf(ctx, "Expected a number")
				fmt.Println()
				continue
			}
			if optionNum < 1 || optionNum > len(options) {
				util.ConsoleLogger().Errorf(ctx, "Expected an option within the range")
				fmt.Println()
				continue
			}
			selectedIdx = optionNum - 1
			break
		}
	}
	selectedOption := options[selectedIdx]
	err := config.Update(key, selectedOption.Id())
	return selectedIdx, err
}

func displayInterfaces(ctx context.Context, i any) []model.DisplayInterface {
	iValue := reflect.Indirect(reflect.ValueOf(i))
	if iValue.Kind() != reflect.Slice {
		util.FileLogger().Fatal(ctx, "Slice must be passed")
	}
	interfaces := make([]model.DisplayInterface, iValue.Len())
	for i := 0; i < iValue.Len(); i++ {
		eValue := iValue.Index(i)
		displayInterface, ok := eValue.Interface().(model.DisplayInterface)
		if !ok {
			util.FileLogger().Fatal(ctx, "Slice element must implement DisplayInterface")
		}
		interfaces[i] = displayInterface
	}
	return interfaces
}
