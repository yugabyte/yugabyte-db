package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import org.apache.commons.collections4.CollectionUtils;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = AuditLogConfigParams.Converter.class)
public class AuditLogConfigParams extends UpgradeTaskParams {

  public boolean installOtelCollector;
  public AuditLogConfig auditLogConfig;

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    boolean exportEnabled =
        auditLogConfig.isExportActive()
            && CollectionUtils.isNotEmpty(auditLogConfig.getUniverseLogsExporterConfig());
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
  }

  public static class Converter extends BaseConverter<AuditLogConfigParams> {}
}
