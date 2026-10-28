/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// GetExportTelemetryConfig fetches the telemetry export configuration of a universe.
// An unconfigured universe returns an empty config rather than an error.
func (a *AuthAPIClient) GetExportTelemetryConfig(
	uUUID string,
) ybav2client.UniverseAPIGetExportTelemetryConfigRequest {
	return a.APIv2Client.UniverseAPI.GetExportTelemetryConfig(a.ctx, a.CustomerUUID, uUUID)
}

// ConfigureExportTelemetryConfig replaces the telemetry export configuration of a universe.
// The API is a whole document replace: any section omitted from the request is disabled.
func (a *AuthAPIClient) ConfigureExportTelemetryConfig(
	uUUID string,
) ybav2client.UniverseAPIConfigureExportTelemetryConfigRequest {
	return a.APIv2Client.UniverseAPI.ConfigureExportTelemetryConfig(a.ctx, a.CustomerUUID, uUUID)
}

// ExportTelemetryConfigYBAVersionCheck checks if the unified export telemetry config API
// can be used
func (a *AuthAPIClient) ExportTelemetryConfigYBAVersionCheck() (bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.YBAAllowExportTelemetryConfigMinStableVersion,
		Preview: util.YBAAllowExportTelemetryConfigMinPreviewVersion,
	}
	return a.CheckValidYBAVersion(allowedVersions)
}

// ServerLogsExportYBAVersionCheck checks if the six server log sections of the unified
// export telemetry config API can be used
func (a *AuthAPIClient) ServerLogsExportYBAVersionCheck() (bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.YBAAllowServerLogsExportMinStableVersion,
		Preview: util.YBAAllowServerLogsExportMinPreviewVersion,
	}
	return a.CheckValidYBAVersion(allowedVersions)
}
