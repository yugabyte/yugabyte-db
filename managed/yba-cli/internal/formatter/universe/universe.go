/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultUniverseListing = "table {{.Name}}\t{{.ProviderCode}}\t{{.UUID}}" +
		"\t{{.Nodes}}\t{{.RF}}\t{{.Version}}\t{{.State}}"
	nodeHeader               = "Number of nodes"
	rfHeader                 = "Replication Factor"
	versionHeader            = "YugabyteDB Version"
	providerCodeHeader       = "Provider Code"
	providerUUIDHeader       = "Provider UUID"
	dedicatedMastersHeader   = "Masters on Dedicated Nodes"
	enableYSQLHeader         = "YSQL Enabled"
	enableYCQLHeader         = "YCQL Enabled"
	accessKeyHeader          = "Access Key"
	useSystemdHeader         = "Use SystemD"
	pricePerDayHeader        = "Price Per Day"
	ysqlAuthEnabledHeader    = "YSQL Auth Enabled"
	ycqlAuthEnabledHeader    = "YCQL Auth Enabled"
	nToNCertHeader           = "Node-to-Node Encryption Certificate"
	nToNTLSHeader            = "Node-to-Node Encryption Enabled"
	cToNCertHeader           = "Client-to-Node Encryption Certificate"
	cToNTLSHeader            = "Client-to-Node Encryption Enabled"
	kmsEnabledHeader         = "Encryption At Rest Enabled"
	kmsConfigNameHeader      = "KMS configuration"
	encryptionRestTypeHeader = "Encryption Type"
	universeOverridesHeader  = "Universe Overrides"
	azOverridesHeader        = "AZ Overrides"
	numMastersHeader         = "Number of Masters"
	numTserversHeader        = "Number of Tservers"
	liveNodesHeader          = "Number of Live Nodes"
	cpuArchitectureHeader    = "CPU Architecture"
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
	// Check if the format is JSON or Pretty JSON
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		// Marshal the slice of universes into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(universes, "", "  ")
		} else {
			output, err = json.Marshal(universes)
		}

		if err != nil {
			logrus.Errorf("Error marshaling universes to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}

	// Existing logic for table and other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, universe := range universes {
			err := format(&Context{u: universe})
			if err != nil {
				logrus.Debugf("Error rendering universe: %v\n", err)
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
		"Name":               formatter.NameHeader,
		"UUID":               formatter.UUIDHeader,
		"ProviderCode":       providerCodeHeader,
		"ProviderUUID":       providerUUIDHeader,
		"AccessKey":          accessKeyHeader,
		"Nodes":              nodeHeader,
		"RF":                 rfHeader,
		"Version":            versionHeader,
		"State":              formatter.StateHeader,
		"DedicatedMasters":   dedicatedMastersHeader,
		"EnableYSQL":         enableYSQLHeader,
		"EnableYCQL":         enableYCQLHeader,
		"UseSystemd":         useSystemdHeader,
		"PricePerDay":        pricePerDayHeader,
		"YSQLAuthEnabled":    ysqlAuthEnabledHeader,
		"YCQLAuthEnabled":    ycqlAuthEnabledHeader,
		"NtoNTLS":            nToNTLSHeader,
		"NtoNCert":           nToNCertHeader,
		"CtoNTLS":            cToNTLSHeader,
		"CtoNCert":           cToNCertHeader,
		"KMSEnabled":         kmsEnabledHeader,
		"KMSConfigName":      kmsConfigNameHeader,
		"EncryptionRestType": encryptionRestTypeHeader,
		"UniverseOverrides":  universeOverridesHeader,
		"AZOverrides":        azOverridesHeader,
		"NumMasters":         numMastersHeader,
		"NumTservers":        numTserversHeader,
		"LiveNodes":          liveNodesHeader,
		"CPUArchitecture":    cpuArchitectureHeader,
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

// PricePerDay fetches the universe instance price per day
func (c *Context) PricePerDay() string {
	return fmt.Sprintf("%f", c.u.GetPricePerHour()*24)
}

// CPUArchitecture fetches CPU architecture of the universe
func (c *Context) CPUArchitecture() string {
	details := c.u.GetUniverseDetails()
	return details.GetArch()
}

// ProviderCode fetches the Cloud provider used in the universe
func (c *Context) ProviderCode() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return userIntent.GetProviderType()
}

// ProviderUUID fetches the Cloud provider used in the universe
func (c *Context) ProviderUUID() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	for _, p := range Providers {
		if strings.Compare(p.GetUuid(), userIntent.GetProvider()) == 0 {
			return fmt.Sprintf("%s(%s)", p.GetName(), p.GetUuid())
		}
	}
	return userIntent.GetProvider()
}

// AccessKey fetches the Cloud provider used in the universe
func (c *Context) AccessKey() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return userIntent.GetAccessKeyCode()
}

// UseSystemd fetches the Cloud provider used in the universe
func (c *Context) UseSystemd() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetUseSystemd())
}

// Nodes fetches the no. of nodes
func (c *Context) Nodes() string {
	details := c.u.GetUniverseDetails()
	numNodes := ""
	for i, c := range details.GetClusters() {
		userIntent := c.GetUserIntent()
		if i != 0 {
			numNodes = fmt.Sprintf("%s,%s", numNodes,
				fmt.Sprintf("%d", userIntent.GetNumNodes()))
		} else {
			numNodes = fmt.Sprintf("%d", userIntent.GetNumNodes())
		}
	}
	return numNodes
}

// RF fetches replication factor of the primary cluster
func (c *Context) RF() string {
	details := c.u.GetUniverseDetails()
	rf := ""
	for i, c := range details.GetClusters() {
		userIntent := c.GetUserIntent()
		if i != 0 {
			rf = fmt.Sprintf("%s,%s", rf,
				fmt.Sprintf("%d", userIntent.GetReplicationFactor()))
		} else {
			rf = fmt.Sprintf("%d", userIntent.GetReplicationFactor())
		}
	}
	return rf
}

// DedicatedMasters fetches if master is placed on a dedicated node
func (c *Context) DedicatedMasters() string {
	details := c.u.GetUniverseDetails()
	dedicatedNodes := ""
	for i, c := range details.GetClusters() {
		userIntent := c.GetUserIntent()
		if i != 0 {
			dedicatedNodes = fmt.Sprintf("%s,%s", dedicatedNodes,
				fmt.Sprintf("%t", userIntent.GetDedicatedNodes()))
		} else {
			dedicatedNodes = fmt.Sprintf("%t", userIntent.GetDedicatedNodes())
		}
	}
	return dedicatedNodes
}

// Version fetches YBDB of the primary cluster
func (c *Context) Version() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return userIntent.GetYbSoftwareVersion()
}

// YSQLAuthEnabled fetches password of the primary cluster
func (c *Context) YSQLAuthEnabled() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetEnableYSQLAuth())
}

// YCQLAuthEnabled fetches password of the primary cluster
func (c *Context) YCQLAuthEnabled() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetEnableYCQLAuth())
}

// EnableYSQL fetches if ysql is enabled
func (c *Context) EnableYSQL() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetEnableYSQL())
}

// EnableYCQL fetches if ycql is enabled
func (c *Context) EnableYCQL() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetEnableYCQL())
}

// NtoNTLS fetches if ntontls is enabled
func (c *Context) NtoNTLS() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetEnableNodeToNodeEncrypt())
}

// CtoNTLS fetches if ctontls is enabled
func (c *Context) CtoNTLS() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetEnableClientToNodeEncrypt())
}

// NtoNCert fetches nton certificate
func (c *Context) NtoNCert() string {
	details := c.u.GetUniverseDetails()
	for _, cert := range Certificates {
		if strings.Compare(cert.GetUuid(), details.GetRootCA()) == 0 {
			return fmt.Sprintf("%s(%s)", cert.GetLabel(), cert.GetUuid())
		}
	}
	return details.GetRootCA()
}

// CtoNCert fetches cton certificate
func (c *Context) CtoNCert() string {
	details := c.u.GetUniverseDetails()
	for _, cert := range Certificates {
		if strings.Compare(cert.GetUuid(), details.GetClientRootCA()) == 0 {
			return fmt.Sprintf("%s(%s)", cert.GetLabel(), cert.GetUuid())
		}
	}
	return details.GetClientRootCA()
}

// KMSEnabled fetches if KMS is enabled or not
func (c *Context) KMSEnabled() string {
	details := c.u.GetUniverseDetails()
	kms := details.GetEncryptionAtRestConfig()
	return fmt.Sprintf("%t", kms.GetEncryptionAtRestEnabled())
}

// KMSConfigName fetches KMS config name
func (c *Context) KMSConfigName() string {
	details := c.u.GetUniverseDetails()
	kms := details.GetEncryptionAtRestConfig()
	for _, k := range KMSConfigs {
		metadataInterface := k["metadata"]
		if metadataInterface != nil {
			metadata := metadataInterface.(map[string]interface{})
			kmsConfigUUID := metadata["configUUID"]
			if kmsConfigUUID != nil && strings.Compare(
				kmsConfigUUID.(string), kms.GetKmsConfigUUID(),
			) == 0 {
				name := metadata["name"]
				if name != nil {
					return name.(string)
				}
			}
		}
	}
	return kms.GetKmsConfigUUID()
}

// EncryptionRestType fetches type of encrytipn key
func (c *Context) EncryptionRestType() string {
	details := c.u.GetUniverseDetails()
	kms := details.GetEncryptionAtRestConfig()
	return kms.GetType()
}

// UniverseOverrides for kubernetes universes
func (c *Context) UniverseOverrides() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	return userIntent.GetUniverseOverrides()
}

// AZOverrides for kubernetes universes
func (c *Context) AZOverrides() string {
	details := c.u.GetUniverseDetails()
	primaryCluster := details.GetClusters()[0]
	userIntent := primaryCluster.GetUserIntent()
	azOverrides := ""
	for k, v := range userIntent.GetAzOverrides() {
		azOverrides = fmt.Sprintf("%s:\n%s\n", k, v)
	}
	if len(azOverrides) > 0 {
		azOverrides = azOverrides[0 : len(azOverrides)-1]
	}
	return azOverrides
}

// NumMasters prints number of masters in given universe
func (c *Context) NumMasters() string {
	numMasters := 0
	details := c.u.GetUniverseDetails()
	nodes := details.GetNodeDetailsSet()
	for _, n := range nodes {
		if n.GetIsMaster() {
			numMasters = numMasters + 1
		}
	}
	return fmt.Sprintf("%d", numMasters)
}

// NumTservers prints number of tservers in given universe
func (c *Context) NumTservers() string {
	numTservers := 0
	details := c.u.GetUniverseDetails()
	nodes := details.GetNodeDetailsSet()
	for _, n := range nodes {
		if n.GetIsTserver() {
			numTservers = numTservers + 1
		}
	}
	return fmt.Sprintf("%d", numTservers)
}

// LiveNodes counts the number of live nodes in the universe
func (c *Context) LiveNodes() string {
	liveNodes := 0
	details := c.u.GetUniverseDetails()
	nodes := details.GetNodeDetailsSet()
	for _, n := range nodes {
		if strings.Compare(n.GetState(), "Live") == 0 {
			liveNodes = liveNodes + 1
		}
	}
	return fmt.Sprintf("%d", liveNodes)
}

// State fetches the state of the universe
func (c *Context) State() string {
	details := c.u.GetUniverseDetails()
	updateInProgress := details.GetUpdateInProgress()
	universePaused := details.GetUniversePaused()
	var allUpdatesSucceeded bool
	errorString := details.GetErrorString()
	if details.GetPlacementModificationTaskUuid() == "" {
		allUpdatesSucceeded = details.GetUpdateSucceeded()
	}
	if !updateInProgress && allUpdatesSucceeded && !universePaused {
		return formatter.Colorize(util.ReadyUniverseState, formatter.GreenColor)
	}
	if !updateInProgress && allUpdatesSucceeded && universePaused {
		return formatter.Colorize(util.PausedUniverseState, formatter.YellowColor)
	}
	if updateInProgress {
		return formatter.Colorize(util.PendingUniverseState, formatter.YellowColor)
	}
	if !updateInProgress && !allUpdatesSucceeded {
		if strings.Compare(errorString, "Preflight checks failed.") == 0 {
			formatter.Colorize(util.WarningUniverseState, formatter.YellowColor)
		} else {
			formatter.Colorize(util.BadUniverseState, formatter.RedColor)
		}
	}
	return formatter.Colorize(util.UnknownUniverseState, formatter.RedColor)
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.u)
}
