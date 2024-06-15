/*
 * Copyright (c) YugaByte, Inc.
 */

package provider

import (
	"encoding/json"
	"fmt"
	"sort"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultProviderListing = "table {{.Name}}\t{{.Code}}\t{{.UUID}}" +
		"\t{{.Regions}}\t{{.SSHUser}}\t{{.SSHPort}}\t{{.Status}}"
	sshPortHeader = "SSH Port"
	sshUserHeader = "SSH User"

	provisionInstanceScriptHeader = "Provision Instance Script"
	skipProvisioningHeader        = "Skip Provisioning"
	enableNodeAgentHeader         = "Enable Node Agent"
	installNodeExporterHeader     = "Install Node Exporter"
	nodeExporterPortHeader        = "Node Exporter Port"
	nodeExporterUserHeader        = "Node Export User"
	setUpChronyHeader             = "Set Up Chrony"
	showSetUpChronyHeader         = "Show Set Up Chrony"
	ntpServers                    = "NTP servers"
	keyPairNameHeader             = "Access Key Pair Name"
)

// Context for provider outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.Provider
}

// NewProviderFormat for formatting output
func NewProviderFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultProviderListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Providers
func Write(ctx formatter.Context, providers []ybaclient.Provider) error {
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, provider := range providers {
			err := format(&Context{p: provider})
			if err != nil {
				logrus.Debugf("Error rendering provider: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewProviderContext(), render)
}

// NewProviderContext creates a new context for rendering provider
func NewProviderContext() *Context {
	providerCtx := Context{}
	providerCtx.Header = formatter.SubHeaderContext{
		"Name":    formatter.NameHeader,
		"UUID":    formatter.UUIDHeader,
		"Regions": formatter.RegionsHeader,
		"Status":  formatter.StatusHeader,
		"Code":    formatter.CodeHeader,
		"SSHPort": sshPortHeader,
		"SSHUser": sshUserHeader,

		"ProvisionInstanceScript": provisionInstanceScriptHeader,
		"SkipProvisioning":        skipProvisioningHeader,
		"EnableNodeAgent":         enableNodeAgentHeader,
		"InstallNodeExporter":     installNodeExporterHeader,
		"NodeExporterPort":        nodeExporterPortHeader,
		"NodeExporterUser":        nodeExporterUserHeader,
		"SetUpChrony":             setUpChronyHeader,
		"ShowSetUpChrony":         showSetUpChronyHeader,
		"NTPServers":              ntpServers,
		"KeyPairName":             keyPairNameHeader,
	}
	return &providerCtx
}

// UUID fetches Provider UUID
func (c *Context) UUID() string {
	return c.p.GetUuid()
}

// Name fetches Provider Name
func (c *Context) Name() string {
	return c.p.GetName()
}

// Code fetches Provider Code
func (c *Context) Code() string {
	return c.p.GetCode()
}

// Status fetches the provider usability state
func (c *Context) Status() string {
	state := c.p.GetUsabilityState()
	if strings.Compare(state, util.ReadyProviderState) == 0 {
		return formatter.Colorize("Ready", formatter.GreenColor)
	}
	if strings.Compare(state, util.ErrorProviderState) == 0 {
		return formatter.Colorize("Error", formatter.RedColor)
	}
	if strings.Compare(state, util.UpdatingProviderState) == 0 {
		return formatter.Colorize("Updating", formatter.YellowColor)
	}
	return formatter.Colorize("Deleting", formatter.YellowColor)
}

// Regions fetches the region name + number of regions associated with the provider
func (c *Context) Regions() string {
	regions := c.p.GetRegions()
	if len(regions) > 1 {
		sort.Slice(regions, func(i, j int) bool {
			return regions[i].GetName() < regions[j].GetName()
		})
		return fmt.Sprintf("%s + %d", regions[0].GetName(), (len(regions) - 1))
	}
	if len(regions) == 1 {
		return regions[0].GetName()
	}
	return ""
}

// SSHPort fetches the SSH port associated with the provider
func (c *Context) SSHPort() string {
	d := c.p.GetDetails()
	port := fmt.Sprintf("%d", d.GetSshPort())
	if len(port) > 0 && d.GetSshPort() != 0 {
		return port
	}
	port = fmt.Sprintf("%d", c.p.GetSshPort())
	return port
}

// SSHUser fetches the SSH user associated with the provider
func (c *Context) SSHUser() string {
	d := c.p.GetDetails()
	user := d.GetSshUser()
	if len(user) > 0 {
		return user
	}
	user = c.p.GetSshUser()
	return user
}

// ProvisionInstanceScript fetches path to provision instance script
func (c *Context) ProvisionInstanceScript() string {
	d := c.p.GetDetails()
	return d.GetProvisionInstanceScript()
}

// NodeExporterPort fetches the node exporter port associated with the provider
func (c *Context) NodeExporterPort() string {
	d := c.p.GetDetails()
	return fmt.Sprintf("%d", d.GetNodeExporterPort())

}

// NodeExporterUser fetches node exporter user associated with the provider
func (c *Context) NodeExporterUser() string {
	d := c.p.GetDetails()
	return d.GetNodeExporterUser()
}

// SkipProvisioning fetches boolean for skipping provisioning during create universe
func (c *Context) SkipProvisioning() string {
	d := c.p.GetDetails()
	return fmt.Sprintf("%t", d.GetSkipProvisioning())
}

// EnableNodeAgent fetches boolean for enabing node agent for setting up universe nodes
func (c *Context) EnableNodeAgent() string {
	d := c.p.GetDetails()
	return fmt.Sprintf("%t", d.GetEnableNodeAgent())
}

// InstallNodeExporter fetches boolean for installing node exporter for prometheus
func (c *Context) InstallNodeExporter() string {
	d := c.p.GetDetails()
	return fmt.Sprintf("%t", d.GetInstallNodeExporter())
}

// SetUpChrony - boolean for SetUpChrony
func (c *Context) SetUpChrony() string {
	d := c.p.GetDetails()
	return fmt.Sprintf("%t", d.GetSetUpChrony())
}

// ShowSetUpChrony - boolean for SetUpChrony
func (c *Context) ShowSetUpChrony() string {
	d := c.p.GetDetails()
	return fmt.Sprintf("%t", d.GetShowSetUpChrony())
}

// NTPServers fetches list of NTP servers
func (c *Context) NTPServers() string {
	d := c.p.GetDetails()
	var ntpServers string
	for _, n := range d.GetNtpServers() {
		ntpServers = fmt.Sprintf("%s %s", ntpServers, n)
	}
	return ntpServers
}

// KeyPairName fetches key pair name from access key
func (c *Context) KeyPairName() string {
	a := c.p.GetAllAccessKeys()
	if len(a) > 0 {
		ki := a[0].GetKeyInfo()
		return ki.GetKeyPairName()
	}
	return c.p.GetKeyPairName()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.p)
}
