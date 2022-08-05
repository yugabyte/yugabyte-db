// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"errors"
	"fmt"
	"net/url"
	"node-agent/app/task"
	"node-agent/model"
	"node-agent/util"
	"strconv"

	"github.com/spf13/cobra"
)

var (
	configureCmd = &cobra.Command{
		Use:   "configure",
		Short: "Configures a node",
		RunE:  configureNodeHandler,
	}
)

func SetupConfigureCommand(parentCmd *cobra.Command) {
	configureCmd.PersistentFlags().
		StringP("setup", "s", "interactive", "Setup style for the configuring the node agent.")
	configureCmd.PersistentFlags().StringP("url", "u", "", "Platform URL")
	configureCmd.PersistentFlags().String("api_token", "", "API Token for fetching config info.")
	configureCmd.PersistentFlags().String("version", "", "Node Agent Version")
	configureCmd.MarkPersistentFlagRequired("api_token")
	configureCmd.MarkPersistentFlagRequired("url")
	configureCmd.MarkPersistentFlagRequired("version")
	parentCmd.AddCommand(configureCmd)
}

func configureNodeHandler(cmd *cobra.Command, args []string) error {
	configureType, err := cmd.Flags().GetString("setup")
	if err != nil {
		util.CliLogger.Errorf("Error while reading flags - %s", err.Error())
		return err
	}
	if configureType == "interactive" {
		if interactiveConfigHandler(cmd) != nil {
			return err
		}
	}
	return nil
}

//Provides a fully interactive configuration setup for the user to configure
//the node agent. It uses api token to fetch customers and finds
//subsequent properties using the customerID.
func interactiveConfigHandler(cmd *cobra.Command) error {
	apiToken, err := cmd.Flags().GetString("api_token")
	if err != nil {
		return errors.New("Need API Token during interactive config setup")
	}
	platformUrl, err := cmd.Flags().GetString("url")
	if err != nil {
		return errors.New("Need Platform URL during interactive config setup")
	}
	ctx := context.Background()
	parsedUrl, err := url.Parse(platformUrl)
	if err != nil {
		return errors.New("Malformed Platform URL passed.")
	}
	version, err := cmd.Flags().GetString("version")
	if err != nil {
		return errors.New("Need Node Agent version for registration.")
	}
	appConfig.Update(util.PlatformVersion, version)
	appConfig.Update(
		util.PlatformHost,
		fmt.Sprintf("%s://%s", parsedUrl.Scheme, parsedUrl.Hostname()),
	)
	appConfig.Update(util.PlatformPort, parsedUrl.Port())
	task.InitHttpClient(appConfig)
	//Get Node Agent name and Node Agent IP
	checkConfigAndUpdate(util.NodeIP, "Node IP")
	checkConfigAndUpdate(util.NodeName, "Node Name")

	//Get Customers from the platform
	response, err := taskExecutor.ExecuteTask(ctx, task.HandleGetCustomers(apiToken))
	if err != nil {
		util.CliLogger.Errorf("Error fetching the customers - %s", err)
		return err
	}
	customersData, ok := response.(*[]model.Customer)
	if !ok {
		panic("Incorrect inference type passed")
	}
	ops := make([]model.DisplayInterface, len(*customersData))
	for i, v := range *customersData {
		ops[i] = v
	}
	customerNum, err := displayOptionsAndGetSelected(ops, "Customer")
	if err != nil {
		util.CliLogger.Errorf("Error while displaying customers: %s", err.Error())
		return err
	}
	appConfig.Update(util.CustomerId, (*customersData)[customerNum].CustomerId)

	// Get Users from the platform.
	response, err = taskExecutor.ExecuteTask(ctx, task.HandleGetUsers(apiToken))
	if err != nil {
		util.CliLogger.Errorf("Error fetching the users - %s", err)
		return err
	}
	usersData, ok := response.(*[]model.User)
	if !ok {
		panic("Incorrect inference type passed")
	}
	ops = make([]model.DisplayInterface, len(*usersData))
	for i, v := range *usersData {
		ops[i] = v
	}
	userNum, err := displayOptionsAndGetSelected(ops, "User")
	if err != nil {
		util.CliLogger.Errorf("Error while displaying customers: %s", err.Error())
		return err
	}
	appConfig.Update(util.UserId, (*usersData)[userNum].UserId)

	// Get Providers from the platform (only on-prem providers displayed)
	response, err = taskExecutor.ExecuteTask(ctx, task.HandleGetProviders(apiToken))
	if err != nil {
		util.CliLogger.Errorf("Error fetching the providers - %s", err)
		return err
	}

	providersData, ok := response.(*[]model.Provider)
	if !ok {
		panic("Incorrect inference type passed")
	}
	i := 0
	for _, data := range *providersData {
		if data.Code == "onprem" {
			(*providersData)[i] = data
			i++
		}
	}

	onpremProviders := (*providersData)[:i]

	ops = make([]model.DisplayInterface, len(onpremProviders))
	for i, v := range onpremProviders {
		ops[i] = v
	}
	providerNum, err := displayOptionsAndGetSelected(ops, "Onprem Provider")
	if err != nil {
		util.CliLogger.Errorf("Error while displaying customers: %s", err.Error())
		return err
	}
	appConfig.Update(util.ProviderId, (onpremProviders)[providerNum].Uuid)

	// Get Instance Types for the provider from the platform.
	response, err = taskExecutor.ExecuteTask(ctx, task.HandleGetInstanceTypes(apiToken))
	if err != nil {
		util.CliLogger.Errorf("Error fetching the instance types - %s", err)
		return err
	}
	instancesData, ok := response.(*[]model.NodeInstanceType)
	if !ok {
		panic("Incorrect inference type passed")
	}
	ops = make([]model.DisplayInterface, len(*instancesData))
	for i, v := range *instancesData {
		ops[i] = v
	}
	instanceNum, err := displayOptionsAndGetSelected(ops, "Instance Type")
	if err != nil {
		util.CliLogger.Errorf("Error while displaying instance Types: %s", err.Error())
		return err
	}
	selectedInstanceType := (*instancesData)[instanceNum]
	appConfig.Update(util.NodeInstanceType, selectedInstanceType.InstanceTypeCode)

	regions := selectedInstanceType.Provider.Regions

	// Select Region.
	ops = make([]model.DisplayInterface, len(regions))
	for i, v := range regions {
		ops[i] = v
	}
	regionNum, err := displayOptionsAndGetSelected(ops, "Region")
	if err != nil {
		util.CliLogger.Errorf("Error while displaying regions: %s", err.Error())
		return err
	}
	appConfig.Update(util.NodeRegion, regions[regionNum].Code)

	// Update availability Zone.
	zones := regions[regionNum].Zones
	ops = make([]model.DisplayInterface, len(zones))
	for i, v := range zones {
		ops[i] = v
	}
	zoneNum, err := displayOptionsAndGetSelected(ops, "Zone")
	if err != nil {
		util.CliLogger.Errorf("Error while displaying zones: %s", err.Error())
		return err
	}
	appConfig.Update(util.NodeAzId, zones[zoneNum].Uuid)
	appConfig.Update(util.NodeZone, zones[zoneNum].Code)
	fmt.Println("* Completed Node Agent Configuration")

	// Update with default values.
	appConfig.Update(util.RequestTimeout, "20")
	appConfig.Update(util.NodePingInterval, "20")

	return registerCmdHandler(cmd, []string{})
}

//Displays the options and prompts the user to select an option followed by validating the option.
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
			util.CliLogger.Errorf("Expected a number")
			fmt.Println()
			continue
		}
		if optionNum < 1 || optionNum > len(options) {
			util.CliLogger.Errorf("Expected an option within the range")
			fmt.Println()
			continue
		}
		return optionNum - 1, nil
	}
}
