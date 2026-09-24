// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.PerfAdvisorEndpointAuth;
import api.v2.models.PerfAdvisorEndpointInfo;
import api.v2.models.PerfAdvisorEndpointMetricsType;
import api.v2.models.PerfAdvisorEndpointSpec;
import api.v2.models.PerfAdvisorEndpointValidationResult;
import api.v2.models.PerfAdvisorEndpointValidationResultChecksInner;
import com.yugabyte.yw.common.pa.PerfAdvisorClient;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuth;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuthType;
import com.yugabyte.yw.models.helpers.paendpoint.PerfAdvisorEndpointType;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.List;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface PerfAdvisorEndpointMapper {

  PerfAdvisorEndpointMapper INSTANCE = Mappers.getMapper(PerfAdvisorEndpointMapper.class);

  /** The mask a password is read back as, and the value an edit echoes back to mean "unchanged". */
  String MASKED_PASSWORD = "********";

  default api.v2.models.PerfAdvisorEndpoint toV2(
      PerfAdvisorEndpoint endpoint, List<Universe> universesUsing) {
    PerfAdvisorEndpointSpec spec = new PerfAdvisorEndpointSpec();
    spec.setName(endpoint.getName());
    spec.setType(api.v2.models.PerfAdvisorEndpointType.fromValue(endpoint.getType().name()));
    spec.setMetricsEndpoint(endpoint.getMetricsEndpoint());
    spec.setMetricsType(PerfAdvisorEndpointMetricsType.fromValue(endpoint.getMetricsType().name()));
    spec.setMetricsAuth(toV2Auth(endpoint.getMetricsAuth()));
    spec.setCollectionEndpoint(endpoint.getCollectionEndpoint());
    spec.setCollectionAuth(toV2Auth(endpoint.getCollectionAuth()));
    spec.setYbmAccountId(endpoint.getYbmAccountId());
    spec.setYbmProjectId(endpoint.getYbmProjectId());

    PerfAdvisorEndpointInfo info = new PerfAdvisorEndpointInfo();
    info.setUuid(endpoint.getUuid());
    info.setCustomerUuid(endpoint.getCustomerUUID());
    info.setUniverseUuids(universesUsing.stream().map(Universe::getUniverseUUID).toList());
    info.setCreateTime(toOffsetDateTime(endpoint.getCreateTime()));
    info.setUpdateTime(toOffsetDateTime(endpoint.getUpdateTime()));

    api.v2.models.PerfAdvisorEndpoint result = new api.v2.models.PerfAdvisorEndpoint();
    result.setSpec(spec);
    result.setInfo(info);
    return result;
  }

  /**
   * Applies a spec onto a record.
   *
   * <p>The prior credentials are passed in rather than read off {@code target}, because on an edit
   * the target <em>is</em> the stored record: reading them back after the first field is written
   * would see the incoming value. They exist so a caller that echoes a masked password back - which
   * is what a read-edit-write round trip through the UI does - keeps the stored one instead of
   * blanking a credential it never saw.
   */
  default PerfAdvisorEndpoint apply(
      PerfAdvisorEndpointSpec spec,
      PerfAdvisorEndpoint target,
      String priorMetricsPassword,
      String priorCollectionPassword) {
    target.setName(spec.getName());
    target.setType(PerfAdvisorEndpointType.valueOf(spec.getType().toString()));
    target.setMetricsEndpoint(spec.getMetricsEndpoint());
    target.setMetricsType(
        PerfAdvisorClient.ExportMetricsType.valueOf(spec.getMetricsType().toString()));
    target.setMetricsAuth(fromV2Auth(spec.getMetricsAuth(), priorMetricsPassword));
    target.setCollectionEndpoint(spec.getCollectionEndpoint());
    target.setCollectionAuth(fromV2Auth(spec.getCollectionAuth(), priorCollectionPassword));
    target.setYbmAccountId(spec.getYbmAccountId());
    target.setYbmProjectId(spec.getYbmProjectId());
    return target;
  }

  default PerfAdvisorEndpointValidationResult toV2(
      PerfAdvisorClient.ExportConfigValidationReport report) {
    PerfAdvisorEndpointValidationResult result = new PerfAdvisorEndpointValidationResult();
    if (report == null) {
      // Nothing to probe from. Reported as valid with no checks rather than as a failure: the
      // configuration has not been contradicted by anything.
      return result.valid(true).checks(List.of());
    }
    result.setValid(report.isValid());
    List<PerfAdvisorClient.ExportConfigValidationCheck> checks =
        report.getChecks() == null ? List.of() : report.getChecks();
    result.setChecks(
        checks.stream()
            .map(
                check ->
                    new PerfAdvisorEndpointValidationResultChecksInner()
                        .field(check.getField())
                        .ok(check.isOk())
                        .message(check.getMessage()))
            .toList());
    return result;
  }

  private static PerfAdvisorEndpointAuth toV2Auth(PaEndpointAuth auth) {
    if (auth == null) {
      return null;
    }
    PerfAdvisorEndpointAuth result = new PerfAdvisorEndpointAuth();
    result.setType(PerfAdvisorEndpointAuth.TypeEnum.fromValue(auth.getType().name()));
    result.setUsername(auth.getUsername());
    // Never read back in the clear.
    result.setPassword(auth.getPassword() == null ? null : MASKED_PASSWORD);
    return result;
  }

  private static PaEndpointAuth fromV2Auth(PerfAdvisorEndpointAuth auth, String priorPassword) {
    if (auth == null) {
      return null;
    }
    PaEndpointAuth result = new PaEndpointAuth();
    result.setType(PaEndpointAuthType.valueOf(auth.getType().toString()));
    result.setUsername(auth.getUsername());
    result.setPassword(
        MASKED_PASSWORD.equals(auth.getPassword()) ? priorPassword : auth.getPassword());
    return result;
  }

  private static OffsetDateTime toOffsetDateTime(Date date) {
    return date == null ? null : Instant.ofEpochMilli(date.getTime()).atOffset(ZoneOffset.UTC);
  }
}
