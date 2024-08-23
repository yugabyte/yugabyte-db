/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"text/template"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultCluster = "table {{.UUID}}\t{{.ClusterNodes}}\t{{.ClusterRF}}" +
		"\t{{.ClusterDedicatedMasters}}\t{{.LinuxVersion}}"
	instanceTable1 = "table {{.InstanceType}}\t{{.VolumeSize}}\t{{.NumVolumes}}" +
		"\t{{.DiskIops}}"
	instanceTable2       = "table {{.Throughput}}\t{{.StorageClass}}\t{{.StorageType}}"
	masterInstanceTable1 = "table {{.MasterInstanceType}}\t{{.MasterVolumeSize}}" +
		"\t{{.MasterNumVolumes}}\t{{.MasterDiskIops}}"
	masterInstanceTable2 = "table {{.MasterThroughput}}\t{{.MasterStorageClass}}" +
		"\t{{.MasterStorageType}}"
	masterGFlagsTable   = "table {{.MasterGFlags}}"
	tserverGFlagsTable  = "table {{.TServerGFlags}}"
	userTagsTable       = "table {{.UserTags}}"
	userTagsHeader      = "User Tags"
	masterGFlagsHeader  = "Master GFlags"
	tserverGFlagsHeader = "TServer GFlags"
	instanceTypeHeader  = "Instance Type"
	volumeSizeHeader    = "Volume Size"
	numVolumesHeader    = "Number of Volumes"
	diskIopsHeader      = "Disk IOPS"
	throughputHeader    = "Throughput"
	storageClassHeader  = "Storage Class"
	storageTypeHeader   = "Storage Type"
	linuxVersionHeader  = "Linux Version"
)

// ClusterContext for cluster outputs
type ClusterContext struct {
	formatter.HeaderContext
	formatter.Context
	c ybaclient.Cluster
}

// NewClusterFormat for formatting output
func NewClusterFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultCluster
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetCluster initializes the context with the cluster data
func (c *ClusterContext) SetCluster(cluster ybaclient.Cluster) {
	c.c = cluster
}

type clusterContext struct {
	Cluster *ClusterContext
}

// Write populates the output table to be displayed in the command line
func (c *ClusterContext) Write(index int) error {
	var err error
	cc := &clusterContext{
		Cluster: &ClusterContext{},
	}
	cc.Cluster.c = c.c

	// Section 1
	tmpl, err := c.startSubsection(defaultCluster)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	clusterType := cc.Cluster.c.GetClusterType()
	if strings.Compare(clusterType, util.ReadReplicaClusterType) == 0 {
		clusterType = "Read Replica"
	} else {
		clusterType = util.PrimaryClusterType
	}
	c.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Cluster %d (%s): Details", index+1, clusterType),
		formatter.BlueColor)))
	c.Output.Write([]byte("\n"))
	if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewClusterContext())
	c.Output.Write([]byte("\n"))

	tmpl, err = c.startSubsection(instanceTable1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.subSection("Instance Type Details")
	if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewClusterContext())
	c.Output.Write([]byte("\n"))

	tmpl, err = c.startSubsection(instanceTable2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewClusterContext())
	c.Output.Write([]byte("\n"))

	userIntent := cc.Cluster.c.GetUserIntent()
	if userIntent.GetDedicatedNodes() {
		tmpl, err = c.startSubsection(masterInstanceTable1)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		c.subSection("Master Instance Type Details")
		if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		c.PostFormat(tmpl, NewClusterContext())
		c.Output.Write([]byte("\n"))

		tmpl, err = c.startSubsection(masterInstanceTable2)
		if err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
			logrus.Errorf("%s", err.Error())
			return err
		}
		c.PostFormat(tmpl, NewClusterContext())
		c.Output.Write([]byte("\n"))
	}

	tmpl, err = c.startSubsection(userTagsTable)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewClusterContext())
	c.Output.Write([]byte("\n"))

	tmpl, err = c.startSubsection(masterGFlagsTable)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewClusterContext())
	c.Output.Write([]byte("\n"))

	tmpl, err = c.startSubsection(tserverGFlagsTable)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := c.ContextFormat(tmpl, cc.Cluster); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	c.PostFormat(tmpl, NewClusterContext())

	// Regions Subsection
	placementInfo := c.c.GetPlacementInfo()
	cloudList := placementInfo.GetCloudList()
	var cloud ybaclient.PlacementCloud
	if len(cloudList) > 0 {
		cloud = cloudList[0]
	}

	logrus.Debugf("Number of Regions: %d", len(cloud.GetRegionList()))
	c.subSection("Regions")
	for i, v := range cloud.GetRegionList() {
		regionContext := NewRegionContext()
		regionContext.Output = os.Stdout
		regionContext.Format = NewFullUniverseFormat(viper.GetString("output"))
		regionContext.SetRegion(v)
		regionContext.Write(i)
	}
	c.Output.Write([]byte("\n"))

	return nil
}

func (c *ClusterContext) startSubsection(format string) (*template.Template, error) {
	c.Buffer = bytes.NewBufferString("")
	c.ContextHeader = ""
	c.Format = formatter.Format(format)
	c.PreFormat()

	return c.ParseFormat()
}

func (c *ClusterContext) subSection(name string) {
	c.Output.Write([]byte("\n\n"))
	c.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	c.Output.Write([]byte("\n"))
}

// NewClusterContext creates a new context for rendering clusters
func NewClusterContext() *ClusterContext {
	clusterCtx := ClusterContext{}
	clusterCtx.Header = formatter.SubHeaderContext{
		"UUID":                    formatter.UUIDHeader,
		"ClusterNodes":            nodeHeader,
		"ClusterRF":               rfHeader,
		"LinuxVersion":            linuxVersionHeader,
		"ClusterDedicatedMasters": dedicatedMastersHeader,
		"MasterGFlags":            masterGFlagsHeader,
		"TServerGFlags":           tserverGFlagsHeader,
		"UserTags":                userTagsHeader,
		"InstanceType":            instanceTypeHeader,
		"VolumeSize":              volumeSizeHeader,
		"NumVolumes":              numVolumesHeader,
		"DiskIops":                diskIopsHeader,
		"Throughput":              throughputHeader,
		"StorageClass":            storageClassHeader,
		"StorageType":             storageTypeHeader,
		"MasterInstanceType":      instanceTypeHeader,
		"MasterVolumeSize":        volumeSizeHeader,
		"MasterNumVolumes":        numVolumesHeader,
		"MasterDiskIops":          diskIopsHeader,
		"MasterThroughput":        throughputHeader,
		"MasterStorageClass":      storageClassHeader,
		"MasterStorageType":       storageTypeHeader,
	}
	return &clusterCtx
}

// UUID fetches Cluster UUID
func (c *ClusterContext) UUID() string {
	return c.c.GetUuid()
}

// ClusterNodes fetches num of nodes in the cluster
func (c *ClusterContext) ClusterNodes() string {
	userIntent := c.c.GetUserIntent()
	return fmt.Sprintf("%d", userIntent.GetNumNodes())
}

// ClusterRF fetches replication factor of the cluster
func (c *ClusterContext) ClusterRF() string {
	userIntent := c.c.GetUserIntent()
	return fmt.Sprintf("%d", userIntent.GetReplicationFactor())
}

// LinuxVersion fetches linux version
func (c *ClusterContext) LinuxVersion() string {
	userIntent := c.c.GetUserIntent()
	imageBundleUUID := userIntent.GetImageBundleUUID()
	providerUUID := userIntent.GetProvider()
	for _, p := range Providers {
		if strings.Compare(p.GetUuid(), providerUUID) == 0 {
			for _, imageBundle := range p.GetImageBundles() {
				if strings.Compare(imageBundle.GetUuid(), imageBundleUUID) == 0 {
					return fmt.Sprintf("%s(%s)", imageBundle.GetName(), imageBundle.GetUuid())
				}
			}
		}
	}

	return imageBundleUUID
}

// ClusterDedicatedMasters fetches boolean
func (c *ClusterContext) ClusterDedicatedMasters() string {
	userIntent := c.c.GetUserIntent()
	return fmt.Sprintf("%t", userIntent.GetDedicatedNodes())
}

// MasterGFlags fetches map as string
func (c *ClusterContext) MasterGFlags() string {
	gflags := ""
	userIntent := c.c.GetUserIntent()
	gFlagsMap := userIntent.GetMasterGFlags()
	for k, v := range gFlagsMap {
		gflags = fmt.Sprintf("%s%s : %s\n", gflags, k, v)
	}
	if len(gflags) == 0 || c.c.GetClusterType() == util.ReadReplicaClusterType {
		return "-"
	}
	gflags = gflags[0 : len(gflags)-1]
	return gflags
}

// TServerGFlags fetches map as string
func (c *ClusterContext) TServerGFlags() string {
	gflags := ""
	userIntent := c.c.GetUserIntent()
	gFlagsMap := userIntent.GetTserverGFlags()
	for k, v := range gFlagsMap {
		gflags = fmt.Sprintf("%s%s : %s\n", gflags, k, v)
	}
	if len(gflags) == 0 {
		return "-"
	}
	gflags = gflags[0 : len(gflags)-1]

	return gflags
}

// UserTags fetches map as string
func (c *ClusterContext) UserTags() string {
	tags := ""
	userIntent := c.c.GetUserIntent()
	tagsMap := userIntent.GetInstanceTags()
	for k, v := range tagsMap {
		tags = fmt.Sprintf("%s%s : %s\n", tags, k, v)
	}
	if len(tags) == 0 {
		return "-"
	}
	tags = tags[0 : len(tags)-1]
	return tags
}

// InstanceType fetches instance type
func (c *ClusterContext) InstanceType() string {
	userIntent := c.c.GetUserIntent()
	return userIntent.GetInstanceType()
}

// MasterInstanceType fetches instance type
func (c *ClusterContext) MasterInstanceType() string {
	userIntent := c.c.GetUserIntent()
	return userIntent.GetMasterInstanceType()
}

// VolumeSize fetches vol size
func (c *ClusterContext) VolumeSize() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetDeviceInfo()
	return fmt.Sprintf("%d", device.GetVolumeSize())
}

// MasterVolumeSize fetches vol size
func (c *ClusterContext) MasterVolumeSize() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetMasterDeviceInfo()
	return fmt.Sprintf("%d", device.GetVolumeSize())
}

// NumVolumes fetches no vols
func (c *ClusterContext) NumVolumes() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetDeviceInfo()
	return fmt.Sprintf("%d", device.GetNumVolumes())
}

// MasterNumVolumes fetches num vols
func (c *ClusterContext) MasterNumVolumes() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetMasterDeviceInfo()
	return fmt.Sprintf("%d", device.GetNumVolumes())
}

// DiskIops fetches DiskIops
func (c *ClusterContext) DiskIops() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetDeviceInfo()
	return fmt.Sprintf("%d", device.GetDiskIops())
}

// MasterDiskIops fetches num vols
func (c *ClusterContext) MasterDiskIops() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetMasterDeviceInfo()
	return fmt.Sprintf("%d", device.GetDiskIops())
}

// Throughput fetches Throughput
func (c *ClusterContext) Throughput() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetDeviceInfo()
	return fmt.Sprintf("%d", device.GetThroughput())
}

// MasterThroughput fetches throughput
func (c *ClusterContext) MasterThroughput() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetMasterDeviceInfo()
	return fmt.Sprintf("%d", device.GetThroughput())
}

// StorageClass fetches StorageClass
func (c *ClusterContext) StorageClass() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetDeviceInfo()
	return device.GetStorageClass()
}

// MasterStorageClass fetches StorageClass
func (c *ClusterContext) MasterStorageClass() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetMasterDeviceInfo()
	return device.GetStorageClass()
}

// StorageType fetches StorageType
func (c *ClusterContext) StorageType() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetDeviceInfo()
	return device.GetStorageType()
}

// MasterStorageType fetches StorageType
func (c *ClusterContext) MasterStorageType() string {
	userIntent := c.c.GetUserIntent()
	device := userIntent.GetMasterDeviceInfo()
	return device.GetStorageType()
}

// MarshalJSON function
func (c *ClusterContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.c)
}
