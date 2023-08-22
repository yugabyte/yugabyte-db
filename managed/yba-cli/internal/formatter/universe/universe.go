/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultUniverseListing = "table {{.Name}}\t{{.ProviderCode}}\t{{.UUID}}" +
		"\t{{.Nodes}}\t{{.RF}}\t{{.Version}}"
	nodeHeader         = "Number of nodes"
	rfHeader           = "Replication Factor"
	versionHeader      = "YugabyteDB Version"
	providerCodeHeader = "Provider Code"
)

// Context for universe outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	u ybaclient.UniverseResp
}

// NewUniverseFormat for formatting output
func NewUniverseFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultUniverseListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Universes
func Write(ctx formatter.Context, universes []ybaclient.UniverseResp) error {
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, universe := range universes {
			err := format(&Context{u: universe})
			if err != nil {
				logrus.Debugf("Error rendering universe: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewUniverseContext(), render)
}

// NewUniverseContext creates a new context for rendering universe
func NewUniverseContext() *Context {
	universeCtx := Context{}
	universeCtx.Header = formatter.SubHeaderContext{
		"Name":         formatter.NameHeader,
		"UUID":         formatter.UUIDHeader,
		"ProviderCode": providerCodeHeader,
		"Nodes":        nodeHeader,
		"RF":           rfHeader,
		"Version":      versionHeader,
	}
	return &universeCtx
}

// UUID fetches Universe UUID
func (c *Context) UUID() string {
	return c.u.GetUniverseUUID()
}

// Name fetches Universe Name
func (c *Context) Name() string {
	return c.u.GetName()
}

// ProviderCode fetches the Cloud provider used in the universe
func (c *Context) ProviderCode() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return userIntent.GetProviderType()
}

// Nodes fetches the no. of nodes
func (c *Context) Nodes() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%d", userIntent.GetNumNodes())
}

// RF fetches replication factor of the primary cluster
func (c *Context) RF() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%d", userIntent.GetReplicationFactor())
}

// Version fetches YBDB of the primary cluster
func (c *Context) Version() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return userIntent.GetYbSoftwareVersion()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.u)
}
