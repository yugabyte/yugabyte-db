// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.CertificateHandler;
import api.v2.models.Certificate;
import api.v2.models.CertificateSpec;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;

public class CertificateApiControllerImp extends CertificateApiControllerImpInterface {
  private final CertificateHandler certificateHandler;

  @Inject
  public CertificateApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      CertificateHandler certificateHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.certificateHandler = certificateHandler;
  }

  @Override
  public Certificate createCertificate(
      Http.Request request, UUID cUUID, CertificateSpec certificateSpec) {
    return certificateHandler.createCertificate(cUUID, certificateSpec);
  }
}
