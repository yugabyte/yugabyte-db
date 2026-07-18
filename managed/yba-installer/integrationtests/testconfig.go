package integrationtests

import (
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/containertest"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

type ConfigValidator func(map[string]any) error

type StateValidator func(ybactlstate.State) error

type TestConfig struct {
	// Manager is the container manager used for the test.
	Manager containertest.Manager

	// Config is the YBAI configuration used for the test.
	Config YBAIConfig

	// InstallConfValidator is a function that validates the YBAI configuration after install
	// Upgrade does the same, just after the yba upgrade if upgrade workflow is used.
	InstallConfValidator ConfigValidator
	UpgradeConfValidator ConfigValidator

	// InstallStateValidator is a function that validates the YBAI state after install
	// Upgrade does the same, just after the yba upgrade if upgrade workflow is used.
	InstallStateValidator StateValidator
	UpgradeStateValidator StateValidator

	container containertest.ContainerRef
}

func (t TestConfig) SetContainer(ctr containertest.ContainerRef) TestConfig {
	t.container = ctr
	return t
}

func (t TestConfig) GetContainer() containertest.ContainerRef {
	return t.container
}

type YBAIConfig struct {
	// Start version is unused for the install test, it is only used for the upgrade test.
	StartVersion string

	// TargetVersion is the main version we are testing against.
	TargetVersion string

	// CustomCerts indicates whether to use custom certificates, or use yba installer self-signed certs.
	CustomCerts bool

	PlatformPort int
}

const LATEST_METADATA_VERSION = "LATEST METADATA VERSION"

func NewYBAIConfigBuilder() YBAIConfigBuilder {
	return YBAIConfigBuilder{
		cfg: YBAIConfig{
			CustomCerts:   false, // Default value
			StartVersion:  "2.20.8.1-b2",
			TargetVersion: LATEST_METADATA_VERSION,
			PlatformPort:  9443,
		},
	}
}

func (b YBAIConfigBuilder) WithCustomCerts(enabled bool) YBAIConfigBuilder {
	b.cfg.CustomCerts = enabled
	return b
}

func (b YBAIConfigBuilder) WithStartVersion(version string) YBAIConfigBuilder {
	b.cfg.StartVersion = version
	return b
}

func (b YBAIConfigBuilder) WithTargetVersion(version string) YBAIConfigBuilder {
	b.cfg.TargetVersion = version
	return b
}

func (b YBAIConfigBuilder) WithPlatformPort(port int) YBAIConfigBuilder {
	b.cfg.PlatformPort = port
	return b
}

type YBAIConfigBuilder struct {
	cfg YBAIConfig
}

func (b YBAIConfigBuilder) Build() YBAIConfig {
	return b.cfg
}

// Test code for allowing chain of validators
