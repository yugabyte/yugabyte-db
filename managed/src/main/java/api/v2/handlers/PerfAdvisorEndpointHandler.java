// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.mappers.PerfAdvisorEndpointMapper;
import api.v2.models.PerfAdvisorEndpointSpec;
import api.v2.models.PerfAdvisorEndpointValidationResult;
import com.google.inject.Inject;
import com.yugabyte.yw.common.pa.PerfAdvisorEndpointService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuth;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PerfAdvisorEndpointHandler {

  @Inject private PerfAdvisorEndpointService endpointService;

  public List<api.v2.models.PerfAdvisorEndpoint> list(UUID cUUID) {
    prepare(cUUID);
    return endpointService.list(cUUID).stream().map(this::toV2).toList();
  }

  public api.v2.models.PerfAdvisorEndpoint get(UUID cUUID, UUID peUUID) {
    prepare(cUUID);
    return toV2(endpointService.getOrBadRequest(cUUID, peUUID));
  }

  public api.v2.models.PerfAdvisorEndpoint create(UUID cUUID, PerfAdvisorEndpointSpec spec) {
    prepare(cUUID);
    PerfAdvisorEndpoint endpoint = new PerfAdvisorEndpoint();
    endpoint.setCustomerUUID(cUUID);
    PerfAdvisorEndpointMapper.INSTANCE.apply(spec, endpoint, null, null);
    return toV2(endpointService.create(endpoint));
  }

  public api.v2.models.PerfAdvisorEndpoint edit(
      UUID cUUID, UUID peUUID, PerfAdvisorEndpointSpec spec) {
    prepare(cUUID);
    PerfAdvisorEndpoint endpoint = endpointService.getOrBadRequest(cUUID, peUUID);
    PerfAdvisorEndpointMapper.INSTANCE.apply(
        spec,
        endpoint,
        password(endpoint.getMetricsAuth()),
        password(endpoint.getCollectionAuth()));
    return toV2(endpointService.update(endpoint));
  }

  public void delete(UUID cUUID, UUID peUUID) {
    prepare(cUUID);
    endpointService.delete(cUUID, peUUID);
  }

  public PerfAdvisorEndpointValidationResult validate(UUID cUUID, PerfAdvisorEndpointSpec spec) {
    prepare(cUUID);
    PerfAdvisorEndpoint candidate = new PerfAdvisorEndpoint();
    candidate.setCustomerUUID(cUUID);
    // Never stored, but the probe is keyed by id on the collector side, so it needs one.
    candidate.generateUUID();
    PerfAdvisorEndpointMapper.INSTANCE.apply(spec, candidate, null, null);
    return PerfAdvisorEndpointMapper.INSTANCE.toV2(endpointService.probe(candidate));
  }

  private void prepare(UUID cUUID) {
    Customer.getOrBadRequest(cUUID);
    endpointService.checkEnabled(cUUID);
  }

  private static String password(PaEndpointAuth auth) {
    return auth == null ? null : auth.getPassword();
  }

  private api.v2.models.PerfAdvisorEndpoint toV2(PerfAdvisorEndpoint endpoint) {
    return PerfAdvisorEndpointMapper.INSTANCE.toV2(
        endpoint, endpointService.universesUsing(endpoint));
  }
}
