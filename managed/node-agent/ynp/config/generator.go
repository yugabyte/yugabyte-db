// Copyright (c) YugabyteDB, Inc.

package config

import (
	"context"
	"fmt"
	"node-agent/app/task/module"
	"node-agent/util"
	"os"
	"os/user"
	"path/filepath"
	"strings"
)

const (
	YnpConfigSchemaPath = "schema/node-agent-provision.json"
	ybUser              = "yugabyte"
)

type GeneratorConfig struct {
	YnpTemplateDir string
	YbaUrl         string
	APIKey         string
	SkipTlsVerify  bool
	NodeIp         string
}

type YNPConfigGenerator struct {
	ctx                  context.Context
	args                 *Args
	resolverDataProvider ResolverDataProvider
	resolvers            map[string]func(context.Context, ResolverDataProvider) (any, error)
}

func NewYNPConfigGenerator(
	ctx context.Context,
	args *Args,
	dataProvider ResolverDataProvider,
) *YNPConfigGenerator {
	gen := &YNPConfigGenerator{
		ctx:                  ctx,
		args:                 args,
		resolverDataProvider: dataProvider,
		resolvers: make(
			map[string]func(context.Context, ResolverDataProvider) (any, error),
		),
	}
	return gen
}

// ValidateAllResolvers validates that all the variables in the template have a corresponding resolver registered.
// This is to catch any missing resolvers before we attempt to generate the config.
func (gen *YNPConfigGenerator) ValidateAllResolvers(templateFilepath string) error {
	testMap := make(map[string]any)
	for name := range gen.resolvers {
		// Dummy to validate that all resolvers are registered. The actual values don't matter for this test.
		testMap[name] = name
	}
	_, err := module.ResolveTemplateStrict(gen.ctx, testMap, templateFilepath, true)
	return err
}

// GenerateTempTemplateYaml generates a temporary jinja templateYAML file from the schema.
// The caller is responsible for deleting the temp file after use.
func GenerateTempTemplateYaml(ynpBasePath string) (string, error) {
	schemaFilepath := filepath.Join(ynpBasePath, YnpConfigSchemaPath)
	schemaHandler, err := NewSchemaHandler(schemaFilepath)
	if err != nil {
		return "", fmt.Errorf("Failed to create schema handler: %w", err)
	}
	templateFile, err := os.CreateTemp("/tmp", "ynp-*.yml")
	if err != nil {
		return "", fmt.Errorf("Failed to create temp file for generated config: %w", err)
	}
	templateFile.Close()
	// Generate the template YAML from the schema and write to the temp file.
	err = schemaHandler.GenerateTemplateYAML(templateFile.Name())
	if err != nil {
		return "", fmt.Errorf("Failed to generate template YAML: %w", err)
	}
	return templateFile.Name(), nil
}

// GenerateYnpConfig generates the YNP config by resolving the template with the registered resolvers.
// It returns the rendered config as a string.
func (gen *YNPConfigGenerator) GenerateYnpConfig() (string, error) {
	err := gen.registerResolvers()
	if err != nil {
		return "", err
	}
	// Generate the jinja template YAML from the schema and write to a temp file.
	templateFilepath, err := GenerateTempTemplateYaml(gen.args.YnpBasePath)
	if err != nil {
		return "", fmt.Errorf("Failed to generate temp template YAML: %w", err)
	}
	defer os.Remove(templateFilepath)
	// Validate that all resolvers are registered and can be resolved without error.
	err = gen.ValidateAllResolvers(templateFilepath)
	if err != nil {
		return "", fmt.Errorf("Resolver validation failed: %w", err)
	}
	// Load any necessary data into the resolver data provider before resolution.
	// This can make remorte calls to fetch data from YBA.
	err = gen.resolverDataProvider.Load(gen.ctx)
	if err != nil {
		util.ConsoleLogger().Errorf(gen.ctx, "Failed to load data resolver: %s", err.Error())
		return "", err
	}
	values := make(map[string]any)
	for name, resolver := range gen.resolvers {
		// Invoke all the resolvers to get the values for the template.
		value, err := resolver(gen.ctx, gen.resolverDataProvider)
		if err != nil {
			return "", fmt.Errorf("Failed to resolve %s: %w", name, err)
		}
		values[name] = value
	}
	// Resolve the template with the values from the resolvers to get the final rendered config.
	rendered, err := module.ResolveTemplate(gen.ctx, values, templateFilepath)
	if err != nil {
		return "", err
	}
	return rendered, nil
}

// Define all the resolvers. Any missing ones will cause the validation test to fail.
// Do not add any remote calls here as the properties will be resolved lazily.
func (gen *YNPConfigGenerator) registerResolvers() error {
	// Variable names are paths in the config separated by underscores.
	// For example, "yba.url" in the config becomes "yba_url" as the variable name in the template and resolver.
	gen.resolvers["logging_directory"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return "./logs", nil
	}
	gen.resolvers["logging_file"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return "app.log", nil
	}
	gen.resolvers["logging_level"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return "INFO", nil
	}
	gen.resolvers["ynp_yb_home_dir"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		return provider.Details.CloudInfo.Onprem.YbHomeDir, nil
	}
	gen.resolvers["ynp_yb_user_home"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		userInfo, err := util.UserInfo(ybUser)
		if err == nil {
			return userInfo.User.HomeDir, nil
		}
		if _, ok := err.(user.UnknownUserError); !ok {
			return nil, err
		}
		// User does not exist, use the yb_home_dir from the provider.
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		return provider.Details.CloudInfo.Onprem.YbHomeDir, nil
	}
	gen.resolvers["ynp_yb_user_id"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		userInfo, err := util.UserInfo(ybUser)
		if err != nil {
			if _, ok := err.(user.UnknownUserError); ok {
				return "994", nil
			}
			return nil, err
		}
		return userInfo.UserID, nil
	}
	gen.resolvers["ynp_chrony_servers"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		return provider.Details.NtpServers, nil
	}
	gen.resolvers["ynp_no_proxy_list"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		// Example: return a list of no-proxy addresses
		return []string{}, nil
	}
	gen.resolvers["ynp_is_airgap"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		return provider.AirGapInstall, nil
	}
	gen.resolvers["ynp_use_system_level_systemd"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return false, nil
	}
	gen.resolvers["ynp_node_ip"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		nodeInstance, err := dataProvider.GetNodeInstance(ctx)
		if err != nil {
			return nil, err
		}
		return nodeInstance.Details.IP, nil
	}
	gen.resolvers["ynp_tmp_directory"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return dataProvider.GetTmpDirectory(ctx)
	}
	gen.resolvers["ynp_is_install_node_agent"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return true, nil
	}
	gen.resolvers["ynp_node_agent_port"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return dataProvider.GetNodeAgentPort(ctx)
	}
	gen.resolvers["ynp_node_exporter_port"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		return provider.Details.NodeExporterPort, nil
	}
	gen.resolvers["ynp_is_configure_clockbound"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		return provider.Details.CloudInfo.Onprem.UseClockbound, nil
	}
	gen.resolvers["ynp_configure_cgroup"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		return provider.Details.CloudInfo.Onprem.EnableMultiTenancy, nil
	}
	gen.resolvers["ynp_configure_thp_settings"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return true, nil
	}
	gen.resolvers["ynp_is_ybcontroller_disabled"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return false, nil
	}
	gen.resolvers["yba_url"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return gen.args.YnpConfigPathValue("yba.url")
	}
	gen.resolvers["yba_skip_tls_verify"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return gen.args.YnpConfigPathValue("yba.skip_tls_verify")
	}

	gen.resolvers["yba_customer_uuid"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		sessionInfo, err := dataProvider.GetSessionInfo(ctx)
		if err != nil {
			return nil, err
		}
		return sessionInfo.CustomerId, nil
	}
	gen.resolvers["yba_api_key"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return gen.args.YnpConfigPathValue("yba.api_key")
	}
	gen.resolvers["yba_node_name"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		nodeInstance, err := dataProvider.GetNodeInstance(ctx)
		if err != nil {
			return nil, err
		}
		return nodeInstance.InstanceName, nil
	}
	gen.resolvers["yba_node_external_fqdn"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		nodeInstance, err := dataProvider.GetNodeInstance(ctx)
		if err != nil {
			return nil, err
		}
		return nodeInstance.Details.IP, nil
	}
	gen.resolvers["yba_provider_name"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		// Example: return the provider name
		return provider.Name(), nil
	}
	gen.resolvers["yba_provider_region_name"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		nodeInstance, err := dataProvider.GetNodeInstance(ctx)
		if err != nil {
			return nil, err
		}
		for _, region := range provider.Regions {
			for _, zone := range region.Zones {
				if zone.Uuid == nodeInstance.ZoneUuid {
					return region.Name(), nil
				}
			}
		}
		return nil, fmt.Errorf("Region not found for zone UUID: %s", nodeInstance.ZoneUuid)
	}
	gen.resolvers["yba_provider_region_zone_name"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		provider, err := dataProvider.GetProvider(ctx)
		if err != nil {
			return nil, err
		}
		nodeInstance, err := dataProvider.GetNodeInstance(ctx)
		if err != nil {
			return nil, err
		}
		for _, region := range provider.Regions {
			for _, zone := range region.Zones {
				if zone.Uuid == nodeInstance.ZoneUuid {
					return zone.Name(), nil
				}
			}
		}
		return nil, fmt.Errorf("Zone not found for zone UUID: %s", nodeInstance.ZoneUuid)
	}
	gen.resolvers["yba_instance_type_name"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		instanceType, err := dataProvider.GetInstanceType(ctx)
		if err != nil {
			return nil, err
		}
		return instanceType.Name(), nil
	}
	gen.resolvers["yba_instance_type_cores"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		instanceType, err := dataProvider.GetInstanceType(ctx)
		if err != nil {
			return nil, err
		}
		return instanceType.NumCores, nil
	}
	gen.resolvers["yba_instance_type_memory_size"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		instanceType, err := dataProvider.GetInstanceType(ctx)
		if err != nil {
			return nil, err
		}
		return instanceType.MemSizeGB, nil
	}
	gen.resolvers["yba_instance_type_volume_size"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		instanceType, err := dataProvider.GetInstanceType(ctx)
		if err != nil {
			return nil, err
		}
		return instanceType.Details.VolumeDetailsList[0].VolumeSize, nil
	}
	gen.resolvers["yba_instance_type_mount_points"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		instanceType, err := dataProvider.GetInstanceType(ctx)
		if err != nil {
			return nil, err
		}
		return strings.Split(instanceType.Details.VolumeDetailsList[0].MountPath, ","), nil
	}
	gen.resolvers["yba_provider_region_latitude"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return 360, nil
	}
	gen.resolvers["yba_provider_region_longitude"] = func(ctx context.Context, dataProvider ResolverDataProvider) (any, error) {
		return 360, nil
	}
	return nil
}
