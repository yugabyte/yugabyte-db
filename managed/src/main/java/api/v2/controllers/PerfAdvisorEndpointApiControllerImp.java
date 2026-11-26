// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.PerfAdvisorEndpointHandler;
import api.v2.models.PerfAdvisorEndpoint;
import api.v2.models.PerfAdvisorEndpointSpec;
import api.v2.models.PerfAdvisorEndpointValidationResult;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.List;
import java.util.UUID;
import play.mvc.Http;

public class PerfAdvisorEndpointApiControllerImp
    extends PerfAdvisorEndpointApiControllerImpInterface {

  private final PerfAdvisorEndpointHandler handler;

  @Inject
  public PerfAdvisorEndpointApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      PerfAdvisorEndpointHandler handler) {
    super(auditService, config, gFlagsAuditHandler);
    this.handler = handler;
  }

  @Override
  public List<PerfAdvisorEndpoint> listPerfAdvisorEndpoints(Http.Request request, UUID cUUID)
      throws Exception {
    return handler.list(cUUID);
  }

  @Override
  public PerfAdvisorEndpoint getPerfAdvisorEndpoint(Http.Request request, UUID cUUID, UUID peUUID)
      throws Exception {
    return handler.get(cUUID, peUUID);
  }

  @Override
  public PerfAdvisorEndpoint createPerfAdvisorEndpoint(
      Http.Request request, UUID cUUID, PerfAdvisorEndpointSpec perfAdvisorEndpointSpec)
      throws Exception {
    return handler.create(cUUID, perfAdvisorEndpointSpec);
  }

  @Override
  public PerfAdvisorEndpoint editPerfAdvisorEndpoint(
      Http.Request request,
      UUID cUUID,
      UUID peUUID,
      PerfAdvisorEndpointSpec perfAdvisorEndpointSpec)
      throws Exception {
    return handler.edit(cUUID, peUUID, perfAdvisorEndpointSpec);
  }

  @Override
  public void deletePerfAdvisorEndpoint(Http.Request request, UUID cUUID, UUID peUUID)
      throws Exception {
    handler.delete(cUUID, peUUID);
  }

  @Override
  public PerfAdvisorEndpointValidationResult validatePerfAdvisorEndpoint(
      Http.Request request, UUID cUUID, PerfAdvisorEndpointSpec perfAdvisorEndpointSpec)
      throws Exception {
    return handler.validate(cUUID, perfAdvisorEndpointSpec);
  }
}
