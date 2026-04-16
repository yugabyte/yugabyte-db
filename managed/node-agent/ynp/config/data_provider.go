// Copyright (c) YugabyteDB, Inc.

package config

import (
	"context"
	"node-agent/model"
	"node-agent/util"
	"node-agent/ynp/yba"
)

// ResolverDataProvider is an interface to fetch necessary data for the resolvers to generate the config.
type ResolverDataProvider interface {
	// Load any necessary data into the resolver data provider before resolution.
	Load(ctx context.Context) error
	// Define methods to fetch necessary data for the resolvers.
	GetSessionInfo(ctx context.Context) (*model.SessionInfo, error)
	GetNodeInstance(ctx context.Context) (*model.NodeInstance, error)
	GetProvider(ctx context.Context) (*model.Provider, error)
	GetInstanceType(ctx context.Context) (*model.NodeInstanceType, error)
	GetTmpDirectory(ctx context.Context) (string, error)
	GetNodeAgentPort(ctx context.Context) (string, error)
}

// DefaultResolverDataProvider is the default implementation of ResolverDataProvider that fetches data from YBA APIs.
type DefaultResolverDataProvider struct {
	args          *Args
	sessionInfo   *model.SessionInfo
	nodeInstance  *model.NodeInstance
	provider      *model.Provider
	instanceType  *model.NodeInstanceType
	tmpDirectory  string
	nodeAgentPort string
}

// NewDefaultResolverDataProvider creates a new instance of DefaultResolverDataProvider with the provided arguments.
func NewDefaultResolverDataProvider(args *Args) *DefaultResolverDataProvider {
	return &DefaultResolverDataProvider{
		args: args,
	}
}

// Load fetches the necessary data from YBA APIs and populates the resolver data provider fields.
func (dp *DefaultResolverDataProvider) Load(ctx context.Context) error {
	var err error
	ybaUrl, err := dp.args.YnpConfigPathValue("yba.url")
	if err != nil {
		return err
	}
	ybaApiKey, err := dp.args.YnpConfigPathValue("yba.api_key")
	if err != nil {
		return err
	}
	skipTlsVerify, err := dp.args.YnpConfigPathValue("yba.skip_tls_verify")
	if err != nil {
		return err
	}
	nodeExternalFqdn, err := dp.args.YnpConfigPathValue("yba.node_external_fqdn")
	if err != nil {
		return err
	}
	util.ConsoleLogger().Infof(ctx, "Fetching data from YBA")
	dp.sessionInfo, err = yba.GetSessionInfo(ctx,
		ybaUrl.(string),
		ybaApiKey.(string),
		skipTlsVerify.(bool),
	)
	if err != nil {
		util.ConsoleLogger().Infof(ctx, "Could not fetch session info - %s", err.Error())
		return err
	}
	dp.nodeInstance, err = yba.GetNodeInstanceByIp(ctx,
		ybaUrl.(string),
		ybaApiKey.(string),
		skipTlsVerify.(bool),
		dp.sessionInfo.CustomerId,
		nodeExternalFqdn.(string),
	)
	if err != nil {
		util.ConsoleLogger().
			Infof(ctx, "Could not fetch node instance by IP/ FQDN %s - %s", nodeExternalFqdn.(string), err.Error())
		return err
	}
	dp.provider, err = yba.GetProviderByZone(ctx,
		ybaUrl.(string),
		ybaApiKey.(string),
		skipTlsVerify.(bool),
		dp.sessionInfo.CustomerId,
		dp.nodeInstance.ZoneUuid,
	)
	if err != nil {
		util.ConsoleLogger().
			Infof(ctx, "Could not fetch provider by zone %s - %s", dp.nodeInstance.ZoneUuid, err.Error())
		return err
	}
	dp.instanceType, err = yba.GetInstanceType(ctx,
		ybaUrl.(string),
		ybaApiKey.(string),
		skipTlsVerify.(bool),
		dp.sessionInfo.CustomerId,
		dp.provider.Uuid,
		dp.nodeInstance.Details.InstanceType,
	)
	if err != nil {
		util.ConsoleLogger().
			Infof(ctx, "Could not fetch instance type %s - %s", dp.nodeInstance.Details.InstanceType, err.Error())
		return err
	}
	dp.tmpDirectory, err = yba.GetRuntimeConfig(ctx,
		ybaUrl.(string),
		ybaApiKey.(string),
		skipTlsVerify.(bool),
		dp.sessionInfo.CustomerId,
		dp.provider.Uuid,
		"yb.filepaths.remoteTmpDirectory",
	)
	if err != nil {
		util.ConsoleLogger().
			Infof(ctx, "Could not fetch runtime config for tmp directory - %s", err.Error())
		return err
	}
	dp.nodeAgentPort, err = yba.GetRuntimeConfig(ctx,
		ybaUrl.(string),
		ybaApiKey.(string),
		skipTlsVerify.(bool),
		dp.sessionInfo.CustomerId,
		util.GlobalRuntimeConfigScopeUuid,
		"yb.node_agent.server.port",
	)
	if err != nil {
		util.ConsoleLogger().
			Infof(ctx, "Could not fetch runtime config for node agent port - %s", err.Error())
		return err
	}
	return nil
}

func (dp *DefaultResolverDataProvider) GetSessionInfo(
	ctx context.Context,
) (*model.SessionInfo, error) {
	return dp.sessionInfo, nil
}

func (dp *DefaultResolverDataProvider) GetNodeInstance(
	ctx context.Context) (*model.NodeInstance, error) {
	return dp.nodeInstance, nil
}

func (dp *DefaultResolverDataProvider) GetProvider(
	ctx context.Context) (*model.Provider, error) {
	return dp.provider, nil
}

func (dp *DefaultResolverDataProvider) GetInstanceType(
	ctx context.Context) (*model.NodeInstanceType, error) {
	return dp.instanceType, nil
}

func (dp *DefaultResolverDataProvider) GetTmpDirectory(
	ctx context.Context) (string, error) {
	return dp.tmpDirectory, nil
}

func (dp *DefaultResolverDataProvider) GetNodeAgentPort(ctx context.Context) (string, error) {
	return dp.nodeAgentPort, nil
}
