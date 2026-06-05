// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.EncryptionAtRestHandler;
import api.v2.models.KmsConfigPagedQuerySpec;
import api.v2.models.KmsConfigPagedResp;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http.Request;

public class EncryptionAtRestApiControllerImp extends EncryptionAtRestApiControllerImpInterface {

  private final EncryptionAtRestHandler handler;

  @Inject
  public EncryptionAtRestApiControllerImp(EncryptionAtRestHandler handler) {
    this.handler = handler;
  }

  @Override
  public KmsConfigPagedResp pageListKmsConfigs(
      Request request, UUID cUUID, KmsConfigPagedQuerySpec kmsConfigPagedQuerySpec)
      throws Exception {
    return handler.pageListKmsConfigs(cUUID, kmsConfigPagedQuerySpec);
  }
}
