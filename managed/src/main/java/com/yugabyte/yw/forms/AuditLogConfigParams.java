package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import org.apache.commons.collections.CollectionUtils;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = AuditLogConfigParams.Converter.class)
public class AuditLogConfigParams extends UpgradeTaskParams {

  public boolean installOtelCollector;
  public AuditLogConfig auditLogConfig;

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    boolean exportEnabled =
        CollectionUtils.isNotEmpty(auditLogConfig.getUniverseLogsExporterConfig());
    if (exportEnabled
        && !universe.getUniverseDetails().otelCollectorEnabled
        && !installOtelCollector) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Universe "
              + universe.getUniverseUUID()
              + " does not have OpenTelemetry Collector "
              + "installed and task params has installOtelCollector=false - can't configure audit "
              + "logs export for the universe");
    }

    if (installOtelCollector && Util.isOnPremManualProvisioning(universe)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Can't install OpenTelemetry Collector for onprem universe with manual provisioning");
    }
    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {}
  }

  public static class Converter extends BaseConverter<AuditLogConfigParams> {}
}
