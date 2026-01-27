// Copyright (c) YugabyteDB, Inc.

package model

import (
	"fmt"
	"time"
)

type BasicInfo struct {
	Uuid   string `json:"uuid,omitempty"`
	Code   string `json:"code,omitempty"`
	Name   string `json:"name,omitempty"`
	Active bool   `json:"active,omitempty"`
}

// Provider is the provider object.
type Provider struct {
	BasicInfo
	Cuuid               string            `json:"customerUUID,omitempty"`
	AirGapInstall       bool              `json:"airGapInstall,omitempty"`
	SshPort             int               `json:"sshPort,omitempty"`
	OverrideKeyValidate bool              `json:"overrideKeyValidate,omitempty"`
	SetUpChrony         bool              `json:"setUpChrony,omitempty"`
	NtpServers          []string          `json:"ntpServers,omitempty"`
	ShowSetUpChrony     bool              `json:"showSetUpChrony,omitempty"`
	Config              map[string]string `json:"config,omitempty"`
	Regions             []Region          `json:"regions,omitempty"`
	Details             ProviderDetails   `json:"details,omitempty"`
	AllAccessKeys       []AccessKey       `json:"allAccessKeys,omitempty"`
}

// ProviderDetails contains the details object within a provider.
// Only the required fields are added here.
type ProviderDetails struct {
	CloudInfo           CloudInfo `json:"cloudInfo"`
	AirGapInstall       bool      `json:"airGapInstall,omitempty"`
	InstallNodeExporter bool      `json:"installNodeExporter,omitempty"`
	NodeExporterPort    int       `json:"nodeExporterPort,omitempty"`
	NodeExporterUser    string    `json:"nodeExporterUser,omitempty"`
	NtpServers          []string  `json:"ntpServers,omitempty"`
	SkipProvisioning    bool      `json:"skipProvisioning,omitempty"`
	EnableNodeAgent     bool      `json:"enableNodeAgent,omitempty"`
}

type CloudInfo struct {
	Onprem OnPremCloudInfo `json:"onprem,omitempty"`
}

type OnPremCloudInfo struct {
	YbHomeDir     string `json:"ybHomeDir,omitempty"`
	UseClockbound bool   `json:"useClockbound,omitempty"`
}

type AccessKey struct {
	KeyInfo KeyInfo `json:"keyInfo"`
}

type KeyInfo struct {
	KeyPairName              string `json:"keyPairName,omitempty"`
	SshPrivateKeyContent     string `json:"sshPrivateKeyContent,omitempty"`
	SkipKeyValidateAndUpload bool   `json:"skipKeyValidateAndUpload,omitempty"`
}

// yyyy-MM-dd HH:mm:ss
type Date time.Time

func (date *Date) UnmarshalJSON(b []byte) error {
	t, err := time.Parse("2006-01-02 15:04:05", string(b))
	if err != nil {
		return err
	}
	*date = Date(t)
	return nil
}

type Region struct {
	BasicInfo
	Longitude float64           `json:"longitude,omitempty"`
	Latitude  float64           `json:"latitude,omitempty"`
	Config    map[string]string `json:"config,omitempty"`
	Zones     []Zone            `json:"zones,omitempty"`
}

type Zone struct {
	BasicInfo
}

type InstanceTypeKey struct {
	InstanceTypeCode string `json:"instanceTypeCode,omitempty"`
}
type NodeInstanceType struct {
	IDKey            InstanceTypeKey         `json:"idKey,omitempty"`
	Active           bool                    `json:"active,omitempty"`
	NumCores         float64                 `json:"numCores,omitempty"`
	MemSizeGB        float64                 `json:"memSizeGB,omitempty"`
	InstanceTypeCode string                  `json:"instanceTypeCode,omitempty"`
	ProviderUuid     string                  `json:"providerUuid,omitempty"`
	Details          NodeInstanceTypeDetails `json:"instanceTypeDetails,omitempty"`
}

type NodeInstanceTypeDetails struct {
	VolumeDetailsList []VolumeDetails `json:"volumeDetailsList,omitempty"`
}

type VolumeDetails struct {
	VolumeType string  `json:"volumeType,omitempty"`
	VolumeSize float64 `json:"volumeSizeGB,omitempty"` //in GB
	MountPath  string  `json:"mountPath,omitempty"`
}

type PreflightCheckVal struct {
	Value string `json:"value,omitempty"`
}

type NodeInstances struct {
	Nodes []NodeDetails `json:"nodes"`
}

type NodeInstance struct {
	NodeUuid             string      `json:"nodeUuid,omitempty"`
	NodeInstanceTypeCode string      `json:"instanceTypeCode,omitempty"`
	NodeName             string      `json:"nodeName,omitempty"`
	InstanceName         string      `json:"instanceName,omitempty"`
	ZoneUuid             string      `json:"zoneUuid,omitempty"`
	State                string      `json:"state,omitempty"`
	Details              NodeDetails `json:"details,omitempty"`
}

type NodeInstanceResponse struct {
	NodeUuid string `json:"nodeUuid,omitempty"`
}

type NodeDetails struct {
	IP           string       `json:"ip,omitempty"`
	Region       string       `json:"region,omitempty"`
	Zone         string       `json:"zone,omitempty"`
	InstanceType string       `json:"instanceType,omitempty"`
	InstanceName string       `json:"instanceName,omitempty"`
	NodeName     string       `json:"nodeName,omitempty"`
	SshUser      string       `json:"sshUser,omitempty"`
	NodeConfigs  []NodeConfig `json:"nodeConfigs,omitempty"`
}

// PreflightCheckParam is the param for PreflightCheckHandler.
type PreflightCheckParam struct {
	SkipProvisioning     bool     `json:"skipProvisioning,omitempty"`
	AirGapInstall        bool     `json:"airGapInstall,omitempty"`
	InstallNodeExporter  bool     `json:"installNodeExporter,omitempty"`
	YbHomeDir            string   `json:"ybHomeDir,omitempty"`
	SshPort              int      `json:"sshPort,omitempty"`
	MountPaths           []string `json:"mountPaths,omitempty"`
	MasterHttpPort       int      `json:"masterHttpPort,omitempty"`
	MasterRpcPort        int      `json:"masterRpcPort,omitempty"`
	TserverHttpPort      int      `json:"tserverHttpPort,omitempty"`
	TserverRpcPort       int      `json:"tserverRpcPort,omitempty"`
	RedisServerHttpPort  int      `json:"redisServerHttpPort,omitempty"`
	RedisServerRpcPort   int      `json:"redisServerRpcPort,omitempty"`
	NodeExporterPort     int      `json:"nodeExporterPort,omitempty"`
	YcqlServerHttpPort   int      `json:"ycqlServerHttpPort,omitempty"`
	YcqlServerRpcPort    int      `json:"ycqlServerRpcPort,omitempty"`
	YsqlServerHttpPort   int      `json:"ysqlServerHttpPort,omitempty"`
	YsqlServerRpcPort    int      `json:"ysqlServerRpcPort,omitempty"`
	YbControllerHttpPort int      `json:"ybControllerHttpPort,omitempty"`
	YbControllerRpcPort  int      `json:"ybControllerRpcPort,omitempty"`
}

type NodeConfig struct {
	Type  string `json:"type,omitempty"`
	Value string `json:"value,omitempty"`
}

type NodeInstanceValidationResponse struct {
	Type        string `json:"type,omitempty"`
	Valid       bool   `json:"valid,omitempty"`
	Required    bool   `json:"required,omitempty"`
	Description string `json:"description,omitempty"`
	Value       string `json:"value,omitempty"`
}

// Id implements the method in DisplayInterface.
func (p Provider) Id() string {
	return p.Uuid
}

// String implements the method in DisplayInterface.
func (p Provider) String() string {
	return fmt.Sprintf("Provider ID: %s, Provider Name: %s", p.Uuid, p.Name())
}

// Name implements the method in DisplayInterface.
func (p Provider) Name() string {
	return p.BasicInfo.Name
}

// Id implements the method in DisplayInterface.
func (i NodeInstanceType) Id() string {
	return i.InstanceTypeCode
}

// String implements the method in DisplayInterface.
func (i NodeInstanceType) String() string {
	return fmt.Sprintf("Instance Code: %s", i.InstanceTypeCode)
}

// Name implements the method in DisplayInterface.
func (i NodeInstanceType) Name() string {
	return i.InstanceTypeCode
}

// Id implements the method in DisplayInterface.
func (r Region) Id() string {
	return r.Code
}

// String implements the method in DisplayInterface.
func (r Region) String() string {
	return fmt.Sprintf("Region ID: %s, Region Code: %s", r.Uuid, r.Code)
}

// Name implements the method in DisplayInterface.
func (r Region) Name() string {
	return r.BasicInfo.Name
}

// Id implements the method in DisplayInterface.
func (z Zone) Id() string {
	return z.Code
}

// String implements the method in DisplayInterface.
func (z Zone) String() string {
	return fmt.Sprintf("Zone ID: %s, Zone Code: %s", z.Uuid, z.Code)
}

// Name implements the method in DisplayInterface.
func (z Zone) Name() string {
	return z.BasicInfo.Name
}
