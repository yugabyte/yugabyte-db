package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = AuditLogConfigParams.Converter.class)
public class AuditLogConfigParams extends UpgradeTaskParams {

  public AuditLogConfig auditLogConfig;

  @Override
  public void verifyParams(Universe universe) {
    super.verifyParams(universe);
  }

  public static class Converter extends BaseConverter<AuditLogConfigParams> {}
}
