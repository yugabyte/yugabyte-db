// Copyright (c) YugaByte, Inc.

package node

import (
	"errors"
	"fmt"
	"node-agent/app/executor"
	"node-agent/app/server"
	"node-agent/app/task"
	"node-agent/model"
	"node-agent/util"
	"reflect"
	"strconv"

	"github.com/spf13/cobra"
)

var (
	configureCmd = &cobra.Command{
		Use:   "configure",
		Short: "Configures a node",
		Run:   configureNodeHandler,
	}
)

func SetupConfigureCommand(parentCmd *cobra.Command) {
	configureCmd.PersistentFlags().String("api_token", "", "API token for fetching config info.")
	configureCmd.PersistentFlags().StringP("url", "u", "", "Platform URL")
	configureCmd.MarkPersistentFlagRequired("api_token")
	configureCmd.MarkPersistentFlagRequired("url")
	parentCmd.AddCommand(configureCmd)
}

func configureNodeHandler(cmd *cobra.Command, args []string) {
	if err := interactiveConfigHandler(cmd); err != nil {
		util.ConsoleLogger().Fatalf("Unable to configure - %s", err.Error())
	}
}

// Provides a fully interactive configuration setup for the user to configure
// the node agent. It uses api token to fetch customers and finds
// subsequent properties using the customer ID.
func interactiveConfigHandler(cmd *cobra.Command) error {
	apiToken, err := cmd.Flags().GetString("api_token")
	if err != nil {
		return errors.New("Need API Token during interactive config setup")
	}
	ctx := server.Context()
	config := util.CurrentConfig()
	_, err = config.StoreCommandFlagString(
		cmd,
		"url",
		util.PlatformUrlKey,
		true,
		util.ExtractBaseURL,
	)
	if err != nil {
		return errors.New("Need Platform URL during interactive config setup")
	}
	// Get node agent name and IP.
	checkConfigAndUpdate(util.NodeIpKey, "Node IP")
	checkConfigAndUpdate(util.NodeNameKey, "Node Name")

	err = server.RetrieveUser(apiToken)
	if err != nil {
		util.ConsoleLogger().Errorf("Error fetching the current user with the API key - %s", err)
		return err
	}
	providersHandler := task.NewGetProvidersHandler(apiToken)
	// Get Providers from the platform (only on-prem providers displayed)
	err = executor.GetInstance(ctx).ExecuteTask(ctx, providersHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Errorf("Error fetching the providers - %s", err)
		return err
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
	providerNum, err := displayOptionsAndGetSelected(
		displayInterfaces(onpremProviders),
		"Onprem Provider",
	)
	if err != nil {
		util.ConsoleLogger().Errorf("Error while displaying providers: %s", err.Error())
		return err
	}
	selectedProvider := onpremProviders[providerNum]
	config.Update(util.ProviderIdKey, selectedProvider.Uuid)

	instanceTypesHandler := task.NewGetInstanceTypesHandler(apiToken)
	// Get Instance Types for the provider from the platform.
	err = executor.GetInstance(ctx).
		ExecuteTask(ctx, instanceTypesHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Errorf("Error fetching the instance types - %s", err)
		return err
	}
	instances := *instanceTypesHandler.Result()
	instanceNum, err := displayOptionsAndGetSelected(
		displayInterfaces(instances),
		"Instance Type",
	)
	if err != nil {
		util.ConsoleLogger().Errorf("Error while displaying instance Types: %s", err.Error())
		return err
	}
	selectedInstanceType := instances[instanceNum]
	config.Update(util.NodeInstanceTypeKey, selectedInstanceType.InstanceTypeCode)

	regions := selectedProvider.Regions
	regionNum, err := displayOptionsAndGetSelected(displayInterfaces(regions), "Region")
	if err != nil {
		util.ConsoleLogger().Errorf("Error while displaying regions: %s", err.Error())
		return err
	}
	config.Update(util.NodeRegionKey, regions[regionNum].Code)

	// Update availability Zone.
	zones := regions[regionNum].Zones
	zoneNum, err := displayOptionsAndGetSelected(displayInterfaces(zones), "Zone")
	if err != nil {
		util.ConsoleLogger().Errorf("Error while displaying zones: %s", err.Error())
		return err
	}
	config.Update(util.NodeAzIdKey, zones[zoneNum].Uuid)
	config.Update(util.NodeZoneKey, zones[zoneNum].Code)
	util.ConsoleLogger().Infof("Completed Node Agent Configuration")

	// Update with default values.
	config.Update(util.RequestTimeoutKey, "20")
	config.Update(util.NodePingIntervalKey, "20")

	err = server.RegisterNodeAgent(server.Context(), apiToken)
	if err != nil {
		util.ConsoleLogger().Fatalf("Unable to register node agent - %s", err.Error())
	}
	util.ConsoleLogger().Info("Node Agent Registration Successful")
	return nil
}

// Displays the options and prompts the user to select an option followed by validating the option.
func displayOptionsAndGetSelected(
	options []model.DisplayInterface,
	displayHead string,
) (int, error) {
	if len(options) == 0 {
		return -1, fmt.Errorf("No record found for %s", displayHead)
	}
	fmt.Printf("* Select your %s\n", displayHead)
	for i, option := range options {
		fmt.Printf("%d. %s\n", i+1, option.ToString())
	}
	for {
		fmt.Printf("\t Enter the option number: ")
		var newVal string
		fmt.Scanln(&newVal)
		optionNum, err := strconv.Atoi(newVal)
		if err != nil {
			util.ConsoleLogger().Errorf("Expected a number")
			fmt.Println()
			continue
		}
		if optionNum < 1 || optionNum > len(options) {
			util.ConsoleLogger().Errorf("Expected an option within the range")
			fmt.Println()
			continue
		}
		return optionNum - 1, nil
	}
}

func displayInterfaces(i any) []model.DisplayInterface {
	iValue := reflect.Indirect(reflect.ValueOf(i))
	if iValue.Kind() != reflect.Slice {
		util.FileLogger().Fatal("Slice must be passed")
	}
	interfaces := make([]model.DisplayInterface, iValue.Len())
	for i := 0; i < iValue.Len(); i++ {
		eValue := iValue.Index(i)
		displayInterface, ok := eValue.Interface().(model.DisplayInterface)
		if !ok {
			util.FileLogger().Fatal("Slice element must implement DisplayInterface")
		}
		interfaces[i] = displayInterface
	}
	return interfaces
}
