package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import play.data.validation.Constraints;

@JsonDeserialize(converter = UpgradeParams.Converter.class)
public class UpgradeParams extends UniverseDefinitionTaskParams {
  // Rolling Restart task type
  @Constraints.Required() public UpgradeTaskParams.UpgradeTaskType taskType;

  // The software version to install. Do not set this value if no software needs to be installed.
  public String ybSoftwareVersion = null;

  // Whenever we run system catalog upgrade after software upgrade or not
  public boolean upgradeSystemCatalog = true;

  public final Map<UUID, String> machineImages = new HashMap<>();
  public boolean forceVMImageUpgrade;

  public boolean forceResizeNode;

  // The certificate that needs to be used.
  public UUID certUUID = null;
  // If the root certificate needs to be rotated.
  public boolean rotateRoot = false;

  // Parameters for toggle tls operation
  public boolean enableNodeToNodeEncrypt = false;
  public boolean enableClientToNodeEncrypt = false;

  @Deprecated
  // This is deprecated use cluster.userIntent.masterGFlags
  public Map<String, String> masterGFlags = new HashMap<String, String>();

  @Deprecated
  // This is deprecated use cluster.userIntent.tserverGFlags
  public Map<String, String> tserverGFlags = new HashMap<String, String>();

  public static final int DEFAULT_SLEEP_AFTER_RESTART_MS = 180000;

  // Time to sleep after a server restart, if there is no way to check/rpc if it is ready to server
  // requests.
  public Integer sleepAfterMasterRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;
  public Integer sleepAfterTServerRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;

  public enum UpgradeOption {
    @JsonProperty("Rolling")
    ROLLING_UPGRADE,
    @JsonProperty("Non-Rolling")
    NON_ROLLING_UPGRADE,
    @JsonProperty("Non-Restart")
    NON_RESTART_UPGRADE
  }

  public UpgradeOption upgradeOption = UpgradeOption.ROLLING_UPGRADE;

  public static class Converter extends BaseConverter<UpgradeParams> {}
}
