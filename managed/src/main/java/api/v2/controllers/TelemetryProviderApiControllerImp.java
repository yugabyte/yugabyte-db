// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.models.TelemetryProviderTypeInfo;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import play.mvc.Http;
import play.mvc.Http.Request;

public class TelemetryProviderApiControllerImp extends TelemetryProviderApiControllerImpInterface {

  @Inject
  public TelemetryProviderApiControllerImp() {}

  @Override
  public List<TelemetryProviderTypeInfo> listTelemetryProviderTypes(
      Request request, UUID cUUID, String exportType) throws Exception {
    Customer.getOrBadRequest(cUUID);

    return Arrays.stream(ProviderType.values())
        .map(this::mapToV2TelemetryProviderTypeInfo)
        .filter(
            info -> {
              if (exportType == null || exportType.isEmpty()) {
                return true;
              }
              switch (exportType.toLowerCase()) {
                case "logs":
                  return info.getIsAllowedForLogs();
                case "metrics":
                  return info.getIsAllowedForMetrics();
                default:
                  throw new PlatformServiceException(
                      Http.Status.BAD_REQUEST,
                      "Invalid exportType. Allowed values: 'logs', 'metrics'");
              }
            })
        .collect(Collectors.toList());
  }

  private TelemetryProviderTypeInfo mapToV2TelemetryProviderTypeInfo(ProviderType type) {
    TelemetryProviderTypeInfo info = new TelemetryProviderTypeInfo();
    // Map the enum value by name
    info.setProviderType(TelemetryProviderTypeInfo.ProviderTypeEnum.valueOf(type.name()));
    info.setIsAllowedForLogs(type.isAllowedForLogs);
    info.setIsAllowedForMetrics(type.isAllowedForMetrics);
    return info;
  }
}
