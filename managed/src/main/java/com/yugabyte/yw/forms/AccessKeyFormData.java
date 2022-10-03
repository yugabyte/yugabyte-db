// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.AccessManager;
import java.util.UUID;
import java.util.List;
import play.data.validation.Constraints;

public class AccessKeyFormData {
  @Constraints.Required() public String keyCode;

  @Constraints.Required() public UUID regionUUID;

  public AccessManager.KeyType keyType;

  public String keyContent;

  public String sshUser;

  public Integer sshPort = 22;

  // Not used anymore. This field should be passed directly to the pre-provision script.
  @Deprecated public boolean passwordlessSudoAccess = true;

  public boolean airGapInstall = false;

  public boolean installNodeExporter = true;

  public String nodeExporterUser = "prometheus";

  public Integer nodeExporterPort = 9300;

  public boolean skipProvisioning = false;

  public boolean setUpChrony = false;

  public List<String> ntpServers;

  // Indicates whether the provider was created before or after PLAT-3009.
  // True if it was created after, else it was created before.
  // This should be true so that all new providers are marked as true by default.
  public boolean showSetUpChrony = true;

  // to be set by user explicitly from UI during key creation
  public Integer expirationThresholdDays;
}
