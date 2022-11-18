/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.Collections;
import java.util.List;

public class ProviderDetails {
  // these are the fields in access key info that actually belong in provider
  @ApiModelProperty public String sshUser;
  @ApiModelProperty public Integer sshPort = 22;
  @ApiModelProperty public boolean airGapInstall = false;
  @ApiModelProperty public List<String> ntpServers = Collections.emptyList();
  @ApiModelProperty public boolean setUpChrony = false;
  // Indicates whether the provider was created before or after PLAT-3009
  // True if it was created after, else it was created before.
  // Dictates whether or not to show the set up NTP option in the provider UI
  @ApiModelProperty public boolean showSetUpChrony = false;

  /// These need database migration before we make these read write
  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  public boolean passwordlessSudoAccess = true;

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  public String provisionInstanceScript = "";

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  public boolean installNodeExporter = true;

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  public Integer nodeExporterPort = 9300;

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  public String nodeExporterUser = "prometheus";

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  public boolean skipProvisioning = false;
}
