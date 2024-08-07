// Copyright (c) YugaByte, Inc.

package node

import (
	"context"
	"errors"
	"fmt"
	"net"
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
	configureCmd.PersistentFlags().
		Bool("silent", false, "Silent configuration with egress enabled.")
	configureCmd.PersistentFlags().String("node_name", "", "Node name")
	configureCmd.PersistentFlags().StringP("node_ip", "n", "", "Node IP")
	configureCmd.PersistentFlags().StringP("bind_ip", "b", "", "Bind IP")
	configureCmd.PersistentFlags().StringP("node_port", "p", "", "Node port")
	/* Required only if egress is disabled. It is used for cloud auto-provisioned nodes.*/
	configureCmd.PersistentFlags().StringP("id", "i", "", "Node agent ID")
	configureCmd.PersistentFlags().String("customer_id", "", "Customer ID")
	configureCmd.PersistentFlags().String("cert_dir", "", "Node agent cert directory")
	/* Required only for silent configuration mode. */
	configureCmd.PersistentFlags().String("provider_id", "", "Provider config ID or name")
	configureCmd.PersistentFlags().String("instance_type", "", "Instance type")
	configureCmd.PersistentFlags().String("region_name", "", "Region name")
	configureCmd.PersistentFlags().String("zone_name", "", "Zone name")

	parentCmd.AddCommand(configureCmd)
}

func configurePreValidator(cmd *cobra.Command, args []string) {
	disabled, err := cmd.Flags().GetBool("disable_egress")
	if err != nil {
		util.ConsoleLogger().
			Fatalf(server.Context(), "Error in reading disable_egress - %s", err.Error())
	}
	silent, err := cmd.Flags().GetBool("silent")
	if err != nil {
		util.ConsoleLogger().
			Fatalf(server.Context(), "Error in reading silent - %s", err.Error())
	}
	if disabled {
		// This mode is for automatic installation of node-agent by YBA.
		cmd.MarkPersistentFlagRequired("id")
		cmd.MarkPersistentFlagRequired("customer_id")
		cmd.MarkPersistentFlagRequired("cert_dir")
		cmd.MarkPersistentFlagRequired("node_name")
		cmd.MarkPersistentFlagRequired("node_ip")
		cmd.MarkPersistentFlagRequired("node_port")
	} else if silent {
		cmd.MarkPersistentFlagRequired("api_token")
		cmd.MarkPersistentFlagRequired("url")
		cmd.MarkPersistentFlagRequired("node_name")
		cmd.MarkPersistentFlagRequired("node_ip")
		cmd.MarkPersistentFlagRequired("node_port")
		cmd.MarkPersistentFlagRequired("provider_id")
		cmd.MarkPersistentFlagRequired("instance_type")
		cmd.MarkPersistentFlagRequired("region_name")
		cmd.MarkPersistentFlagRequired("zone_name")
	} else {
		cmd.MarkPersistentFlagRequired("api_token")
		cmd.MarkPersistentFlagRequired("url")
	}
}

func configureNodeHandler(cmd *cobra.Command, args []string) {
	ctx := server.Context()
	if disabled, err := cmd.Flags().GetBool("disable_egress"); err != nil {
		util.ConsoleLogger().
			Fatalf(server.Context(), "Error in reading disable_egress - %s", err.Error())
	} else if disabled {
		configureDisabledEgress(ctx, cmd)
	} else {
		configureEnabledEgress(ctx, cmd)
	}
}

func configureDisabledEgress(ctx context.Context, cmd *cobra.Command) {
	config := util.CurrentConfig()
	_, err := config.StoreCommandFlagString(
		ctx,
		cmd,
		"id",
		util.NodeAgentIdKey,
		nil,  /* default value */
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
		nil,  /* default value */
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
		nil,  /* default value */
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent cert dir - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_name",
		util.NodeNameKey,
		nil,  /* default value */
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node name - %s", err.Error())
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
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent IP - %s", err.Error())
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
			util.ConsoleLogger().
				Fatalf(ctx, "Unable to store node agent bind IP - %s", err.Error())
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
		nil,  /* default value */
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

// Provides a fully interactive configuration and silent setup for the user to configure
// the node agent. It uses api token to fetch customers and finds subsequent properties
// using the customer ID.
func configureEnabledEgress(ctx context.Context, cmd *cobra.Command) {
	silent, err := cmd.Flags().GetBool("silent")
	if err != nil {
		util.ConsoleLogger().
			Fatalf(server.Context(), "Error in reading silent - %s", err.Error())
	}
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
		nil,  /* default value */
		true, /* isRequired */
		util.ExtractBaseURL,
	)
	if err != nil {
		util.ConsoleLogger().
			Fatalf(ctx, "Need Platform URL during interactive config setup - %s", err.Error())
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
	err = server.RetrieveUser(ctx, apiToken)
	if err != nil {
		util.ConsoleLogger().Fatalf(
			ctx,
			"Error fetching the current user with the API key - %s", err.Error())
	}
	_, err = config.StoreCommandFlagString(
		ctx,
		cmd,
		"node_port",
		util.NodePortKey,
		nil,  /* default value */
		true, /* isRequired */
		nil,  /* validator */
	)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent port - %s", err.Error())
	}
	if silent {
		_, err = config.StoreCommandFlagString(
			ctx,
			cmd,
			"node_name",
			util.NodeNameKey,
			nil,  /* default value */
			true, /* isRequired */
			nil,  /* validator */
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to store node name - %s", err.Error())
		}
	} else {
		checkConfigAndUpdate(ctx, util.NodeNameKey, nil, "Node Name")
	}
	if silent {
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
			util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent IP - %s", err.Error())
		}
	} else {
		// Get node agent name and IP.
		checkConfigAndUpdate(ctx, util.NodeIpKey, nil, "Node IP")
	}
	nodeIp := config.String(util.NodeIpKey)
	parsedIp := net.ParseIP(nodeIp)
	if parsedIp == nil {
		// Get the bind IP if it is DNS. It defaults to the DNS if it is not present.
		if silent {
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
				util.ConsoleLogger().
					Fatalf(ctx, "Unable to store node agent bind IP - %s", err.Error())
			}
		} else {
			checkConfigAndUpdate(ctx, util.NodeBindIpKey, &nodeIp, "Bind IP")
		}
	} else {
		// Use the node IP as the bind IP.
		err = config.Update(util.NodeBindIpKey, nodeIp)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to store node agent bind IP - %s", err.Error())
		}
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
		// Only onprem and manually provisioned nodes.
		if data.Code == "onprem" && data.Details.SkipProvisioning {
			providers[i] = data
			i++
		}
	}
	onpremProviders := providers[:i]
	var selectedProvider model.Provider
	if silent {
		_, err = config.StoreCommandFlagString(
			ctx,
			cmd,
			"provider_id",
			util.ProviderIdKey,
			nil,  /* default value */
			true, /* isRequired */
			func(in string) (string, error) {
				for _, pr := range onpremProviders {
					// Overload the option to use either name or provider ID for backward compatibility.
					if pr.Id() == in || pr.Name() == in {
						selectedProvider = pr
						return pr.Id(), nil
					}
				}
				return "", errors.New("Provider is not found")
			},
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to store provider ID - %s", err.Error())
		}
	} else {
		providerNum, err := displayOptionsAndUpdateSelected(
			ctx,
			util.ProviderIdKey,
			displayInterfaces(ctx, onpremProviders),
			"Onprem Manually Provisioned Provider",
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Error while displaying providers - %s", err.Error())
		}
		selectedProvider = onpremProviders[providerNum]
	}

	instanceTypesHandler := task.NewGetInstanceTypesHandler(apiToken)
	// Get Instance Types for the provider from the platform.
	err = executor.GetInstance().
		ExecuteTask(ctx, instanceTypesHandler.Handle)
	if err != nil {
		util.ConsoleLogger().Fatalf(ctx, "Error fetching the instance types - %s", err.Error())
	}
	instances := *instanceTypesHandler.Result()
	if silent {
		_, err = config.StoreCommandFlagString(
			ctx,
			cmd,
			"instance_type",
			util.NodeInstanceTypeKey,
			nil,  /* default value */
			true, /* isRequired */
			func(in string) (string, error) {
				for _, instance := range instances {
					if instance.Id() == in {
						return instance.Id(), nil
					}
				}
				return "", errors.New("Instance type is not found")
			},
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to store instance type - %s", err.Error())
		}
	} else {
		_, err = displayOptionsAndUpdateSelected(
			ctx,
			util.NodeInstanceTypeKey,
			displayInterfaces(ctx, instances),
			"Instance Type",
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Error while displaying instance types - %s", err.Error())
		}
	}
	// Update availability Zone.
	if silent {
		var selectedRegion model.Region
		_, err = config.StoreCommandFlagString(
			ctx,
			cmd,
			"region_name",
			util.NodeRegionKey,
			nil,  /* default value */
			true, /* isRequired */
			func(in string) (string, error) {
				for _, region := range selectedProvider.Regions {
					if region.Id() == in {
						selectedRegion = region
						return region.Id(), nil
					}
				}
				return "", errors.New("Region name is not found")
			},
		)
		_, err = config.StoreCommandFlagString(
			ctx,
			cmd,
			"zone_name",
			util.NodeZoneKey,
			nil,  /* default value */
			true, /* isRequired */
			func(in string) (string, error) {
				for _, zone := range selectedRegion.Zones {
					if zone.Id() == in {
						config.Update(util.NodeAzIdKey, zone.Uuid)
						return zone.Id(), nil
					}
				}
				return "", errors.New("Zone name is not found")
			},
		)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to store zone ID - %s", err.Error())
		}
	} else {
		regions := selectedProvider.Regions
		regionNum, err := displayOptionsAndUpdateSelected(
			ctx,
			util.NodeRegionKey, displayInterfaces(ctx, regions), "Region")
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Error while displaying regions - %s", err.Error())
		}
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
	}
	util.ConsoleLogger().Infof(ctx, "Completed Node Agent Configuration")
	util.ConsoleLogger().Infof(ctx, "Checking for existing Node Agent with IP %s", nodeIp)
	err = server.ValidateNodeAgentIfExists(server.Context(), apiToken)
	if err == nil {
		util.ConsoleLogger().Infof(ctx, "Node Agent is already registered with IP %s", nodeIp)
	} else if err == util.ErrNotExist {
		util.ConsoleLogger().Infof(ctx, "Registering Node Agent with IP %s", nodeIp)
		err = server.RegisterNodeAgent(server.Context(), apiToken)
		if err != nil {
			util.ConsoleLogger().Fatalf(ctx, "Unable to register node agent - %s", err.Error())
		}
	} else {
		util.ConsoleLogger().Fatalf(ctx, "Unable to check for existing node agent - %s", err.Error())
	}
	util.ConsoleLogger().Info(ctx, "Node Agent Configuration Successful")
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
