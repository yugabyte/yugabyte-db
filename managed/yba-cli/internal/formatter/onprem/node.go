/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultNodesListing = "table {{.IP}}\t{{.NodeName}}\t{{.NodeUUID}}" +
		"\t{{.Region}}\t{{.Zone}}\t{{.InstanceType}}\t{{.UniverseName}}"

	nodeNameHeader     = "Node Name"
	ipHeader           = "IP"
	nodeUUIDHeader     = "Node UUID"
	regionHeader       = "Region"
	zoneHeader         = "Zone"
	instanceTypeHeader = "Instance Type"
	universeNameHeader = "Universe Name"
)

// UniverseList holds list of universes to fetch status of the node instance
var UniverseList []ybaclient.UniverseResp

// Context for node outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	n ybaclient.NodeInstance
}

// NewNodesFormat for formatting output
func NewNodesFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultNodesListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Nodess
func Write(ctx formatter.Context, nodes []ybaclient.NodeInstance) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of nodes into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(nodes, "", "  ")
		} else {
			output, err = json.Marshal(nodes)
		}

		if err != nil {
			logrus.Errorf("Error marshaling nodes to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, node := range nodes {
			err := format(&Context{n: node})
			if err != nil {
				logrus.Debugf("Error rendering node: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewNodesContext(), render)
}

// NewNodesContext creates a new context for rendering node
func NewNodesContext() *Context {
	instanceTypeCtx := Context{}
	instanceTypeCtx.Header = formatter.SubHeaderContext{
		"NodeName":     nodeNameHeader,
		"IP":           ipHeader,
		"NodeUUID":     nodeUUIDHeader,
		"Region":       regionHeader,
		"Zone":         zoneHeader,
		"InstanceType": instanceTypeHeader,
		"UniverseName": universeNameHeader,
	}
	return &instanceTypeCtx
}

// NodeName of the node
func (c *Context) NodeName() string {
	return c.n.GetInstanceName()
}

// IP of the node
func (c *Context) IP() string {
	details := c.n.GetDetails()
	return details.GetIp()
}

// NodeUUID of the node
func (c *Context) NodeUUID() string {
	return c.n.GetNodeUuid()
}

// Region of the node
func (c *Context) Region() string {
	details := c.n.GetDetails()
	return details.GetRegion()
}

// Zone of the node
func (c *Context) Zone() string {
	details := c.n.GetDetails()
	return details.GetZone()
}

// InstanceType of the node
func (c *Context) InstanceType() string {
	details := c.n.GetDetails()
	return details.GetInstanceType()
}

// UniverseName of the node in use
func (c *Context) UniverseName() string {
	inUse := c.n.GetInUse()
	if !inUse {
		return "Not Used"
	}
	nodeName := c.n.GetNodeName()
	for _, u := range UniverseList {
		details := u.GetUniverseDetails()
		for _, node := range details.GetNodeDetailsSet() {
			if node.GetNodeName() == nodeName {
				return u.GetName()
			}
		}
	}
	return formatter.Colorize("Associated universe not found", formatter.RedColor)
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.n)
}
