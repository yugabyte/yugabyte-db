/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cmd

import (
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

// optionalService is a service that yba-ctl.yml can enable or disable after install. configKey is
// the yba-ctl.yml flag; stateFlag is the state field that records whether the last install,
// upgrade or reconfigure left the service installed.
type optionalService struct {
	name      string
	configKey string
	stateFlag func(*ybactlstate.State) *bool
}

var optionalServices = []optionalService{
	{PerfAdvisorServiceName, "perfAdvisor.enabled",
		func(s *ybactlstate.State) *bool { return &s.Services.PerfAdvisor }},
	{NodeExporterServiceName, "nodeExporter.enabled",
		func(s *ybactlstate.State) *bool { return &s.Services.NodeExporter }},
	{ByocApiProxyServiceName, "byocApiProxy.enabled",
		func(s *ybactlstate.State) *bool { return &s.Services.ByocApiProxy }},
}

// optionalServiceByName returns the optional service called name, or nil for a core service.
func optionalServiceByName(name string) *optionalService {
	for i := range optionalServices {
		if optionalServices[i].name == name {
			return &optionalServices[i]
		}
	}
	return nil
}

func (o optionalService) enabled() bool {
	return viper.GetBool(o.configKey)
}

func (o optionalService) installed(state *ybactlstate.State) bool {
	return *o.stateFlag(state)
}

func (o optionalService) setInstalled(state *ybactlstate.State, installed bool) {
	*o.stateFlag(state) = installed
}
