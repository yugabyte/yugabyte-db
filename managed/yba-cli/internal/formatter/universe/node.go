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
	defaultNodeListing = "table {{.NodeName}}\t{{.NodeUUID}}\t{{.IP}}" +
		"\t{{.State}}\t{{.IsMaster}}\t{{.IsTserver}}\t{{.MasterState}}"

	nodeNameHeader    = "Node Name"
	ipHeader          = "IP"
	nodeUUIDHeader    = "Node UUID"
	isMasterHeader    = "Is Master process running"
	isTserverHeader   = "Is Tserver process running"
	masterStateHeader = "Master state"
)

// NodeContext for node outputs
type NodeContext struct {
	formatter.HeaderContext
	formatter.Context
	n ybaclient.NodeDetailsResp
}

// NewNodesFormat for formatting output
func NewNodesFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultNodeListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NodeWrite renders the context for a list of Node
func NodeWrite(ctx formatter.Context, nodes []ybaclient.NodeDetailsResp) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of universe nodes into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(nodes, "", "  ")
		} else {
			output, err = json.Marshal(nodes)
		}

		if err != nil {
			logrus.Errorf("Error marshaling universe nodes to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, node := range nodes {
			err := format(&NodeContext{n: node})
			if err != nil {
				logrus.Debugf("Error rendering node: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewNodeContext(), render)
}

// NewNodeContext creates a new context for rendering node
func NewNodeContext() *NodeContext {
	instanceTypeCtx := NodeContext{}
	instanceTypeCtx.Header = formatter.SubHeaderContext{
		"NodeName":    nodeNameHeader,
		"IP":          ipHeader,
		"NodeUUID":    nodeUUIDHeader,
		"State":       formatter.StateHeader,
		"IsMaster":    isMasterHeader,
		"IsTserver":   isTserverHeader,
		"MasterState": masterStateHeader,
	}
	return &instanceTypeCtx
}

// NodeName of the node
func (c *NodeContext) NodeName() string {
	return c.n.GetNodeName()
}

// IP of the node
func (c *NodeContext) IP() string {
	cloudInfo := c.n.GetCloudInfo()
	return cloudInfo.GetPrivateIp()
}

// NodeUUID of the node
func (c *NodeContext) NodeUUID() string {
	return c.n.GetNodeUuid()
}

// State of the node
func (c *NodeContext) State() string {
	return c.n.GetState()
}

// IsMaster status of the node
func (c *NodeContext) IsMaster() string {
	return fmt.Sprintf("%t", c.n.GetIsMaster())
}

// MasterState of the node
func (c *NodeContext) MasterState() string {
	if c.n.GetIsMaster() {
		return c.n.GetMasterState()
	}
	return "-"
}

// IsTserver status of the node
func (c *NodeContext) IsTserver() string {
	return fmt.Sprintf("%t", c.n.GetIsTserver())
}

// MarshalJSON function
func (c *NodeContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.n)
}
