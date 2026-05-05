/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import ybav2client "github.com/yugabyte/platform-go-client/v2"

// RunScript executes a script on database nodes in a universe
func (a *AuthAPIClient) RunScript(
	uUUID string,
) ybav2client.UniverseAPIRunScriptRequest {
	return a.APIv2Client.UniverseAPI.RunScript(a.ctx, a.CustomerUUID, uUUID)
}
