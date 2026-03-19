/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ha

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultHAConfigListing = "table {{.UUID}}\t{{.ClusterKey}}\t{{.GlobalState}}" +
		"\t{{.LastFailover}}\t{{.AcceptAnyCertificate}}"

	defaultInstancesListing = "table {{.Address}}\t{{.InstanceState}}\t{{.LastBackup}}\t{{.UUID}}\t{{.IsLeader}}"

	uuidHeader                 = "UUID"
	clusterKeyHeader           = "Cluster Key"
	globalStateHeader          = "Global State"
	lastFailoverHeader         = "Last Failover"
	acceptAnyCertificateHeader = "Accept Any Certificate"
	instancesHeader            = "Instances"

	addressHeader       = "Address"
	instanceStateHeader = "Instance State"
	lastBackupHeader    = "Last Backup"
	instanceUUIDHeader  = "UUID"
	isLeaderHeader      = "Instance Type"
	isLocalHeader       = "Is Local"
)

// Context for HA config outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	c *ybaclient.HAConfigGetResp
}

// InstanceContext for HA instance outputs
type InstanceContext struct {
	formatter.HeaderContext
	formatter.Context
	instance map[string]interface{}
}

// NewHAFormat for formatting output
func NewHAFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultHAConfigListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewInstancesFormat for formatting instances output
func NewInstancesFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultInstancesListing
		return formatter.Format(format)
	default:
		return formatter.Format(source)
	}
}

// Write renders the context for HA config
func Write(ctx formatter.Context, haConfig *ybaclient.HAConfigGetResp) error {
	// Check if the format is JSON or Pretty JSON
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(haConfig, "", "  ")
		} else {
			output, err = json.Marshal(haConfig)
		}

		if err != nil {
			logrus.Errorf("Error marshaling HA config to json: %v\n", err)
			return err
		}

		_, err = ctx.Output.Write(output)
		return err
	}

	render := func(format func(subContext formatter.SubContext) error) error {
		return format(&Context{c: haConfig})
	}
	err := ctx.Write(NewHAContext(), render)
	if err != nil {
		return err
	}
	// If there are instances, render them in a separate table

	if len(haConfig.Instances) > 0 {
		ctx.Output.Write([]byte("\n"))
		ctx.Output.Write([]byte(formatter.Colorize("Instances", formatter.GreenColor)))
		ctx.Output.Write([]byte("\n"))

		// Create a new context for instances table
		instancesCtx := formatter.Context{
			Output:  ctx.Output,
			Format:  NewInstancesFormat(viper.GetString("output")),
			Command: ctx.Command,
		}

		instancesRender := func(format func(subContext formatter.SubContext) error) error {
			for _, instanceInterface := range haConfig.Instances {
				// Convert interface{} to map[string]interface{}
				instanceBytes, err := json.Marshal(instanceInterface)
				if err != nil {
					logrus.Errorf("Error marshaling instance: %v\n", err)
					continue
				}

				var instanceMap map[string]interface{}
				if err := json.Unmarshal(instanceBytes, &instanceMap); err != nil {
					logrus.Errorf("Error unmarshaling instance: %v\n", err)
					continue
				}

				err = format(&InstanceContext{instance: instanceMap})
				if err != nil {
					logrus.Debugf("Error rendering instance: %v", err)
					return err
				}
			}
			return nil
		}

		err = instancesCtx.Write(NewInstanceContext(), instancesRender)
		if err != nil {
			return err
		}
	}

	return nil
}

// NewHAContext creates a new context for rendering HA config
func NewHAContext() *Context {
	haCtx := Context{}
	haCtx.Header = formatter.SubHeaderContext{
		"UUID":                 uuidHeader,
		"ClusterKey":           clusterKeyHeader,
		"GlobalState":          globalStateHeader,
		"LastFailover":         lastFailoverHeader,
		"AcceptAnyCertificate": acceptAnyCertificateHeader,
		"Instances":            instancesHeader,
	}
	return &haCtx
}

// NewInstanceContext creates a new context for rendering instances
func NewInstanceContext() *InstanceContext {
	instanceCtx := InstanceContext{}
	instanceCtx.Header = formatter.SubHeaderContext{
		"Address":       addressHeader,
		"InstanceState": instanceStateHeader,
		"LastBackup":    lastBackupHeader,
		"UUID":          instanceUUIDHeader,
		"IsLeader":      isLeaderHeader,
		"IsLocal":       isLocalHeader,
	}
	return &instanceCtx
}

// UUID fetches HA Config UUID
func (c *Context) UUID() string {
	return c.c.GetUuid()
}

// ClusterKey fetches HA Config Cluster Key
func (c *Context) ClusterKey() string {
	return c.c.GetClusterKey()
}

// GlobalState fetches HA Config Global State
func (c *Context) GlobalState() string {
	return c.c.GetGlobalState()
}

// LastFailover fetches HA Config Last Failover
func (c *Context) LastFailover() string {
	if c.c.GetLastFailover().String() == "" {
		return "-"
	}
	return c.c.GetLastFailover().String()
}

// AcceptAnyCertificate fetches HA Config Accept Any Certificate
func (c *Context) AcceptAnyCertificate() string {
	return fmt.Sprintf("%t", c.c.GetAcceptAnyCertificate())
}

// Instances fetches HA Config Instances
func (c *Context) Instances() string {
	if len(c.c.Instances) == 0 {
		return "-"
	}
	data, err := json.MarshalIndent(c.c.Instances, "", "  ")
	if err != nil {
		return fmt.Sprintf("Error marshaling instances: %v", err)
	}
	return string(data)
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.c)
}

// Address fetches Instance Address
func (c *InstanceContext) Address() string {
	if val, ok := c.instance["address"].(string); ok {
		return val
	}
	return "-"
}

// InstanceState fetches Instance State
func (c *InstanceContext) InstanceState() string {
	if val, ok := c.instance["instance_state"].(string); ok {
		return val
	}
	return "-"
}

// LastBackup fetches Instance Last Backup
func (c *InstanceContext) LastBackup() string {
	if val, ok := c.instance["last_backup"].(string); ok {
		return val
	}
	return "-"
}

// UUID fetches Instance UUID
func (c *InstanceContext) UUID() string {
	if val, ok := c.instance["uuid"].(string); ok {
		return val
	}
	return "-"
}

// IsLeader tells if instance is leader
func (c *InstanceContext) IsLeader() string {
	if val, ok := c.instance["is_leader"].(bool); ok {
		if val == true {
			return "Active"
		}
		return "Standby"
	}
	return "-"
}

// IsLocal tells if instance is local
func (c *InstanceContext) IsLocal() string {
	if val, ok := c.instance["is_local"].(bool); ok {
		return fmt.Sprintf("%t", val)
	}
	return "-"
}

// MarshalJSON function for InstanceContext
func (c *InstanceContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.instance)
}
