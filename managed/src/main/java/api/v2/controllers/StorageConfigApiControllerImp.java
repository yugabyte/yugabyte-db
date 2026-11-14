// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.StorageConfigHandler;
import api.v2.models.StorageConfig;
import api.v2.models.StorageConfigSpec;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.util.UUID;
import play.mvc.Http;

public class StorageConfigApiControllerImp extends StorageConfigApiControllerImpInterface {
  private final StorageConfigHandler storageConfigHandler;

  @Inject
  public StorageConfigApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      StorageConfigHandler storageConfigHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.storageConfigHandler = storageConfigHandler;
  }

  @Override
  public YBATask createStorageConfig(
      Http.Request request, UUID cUUID, StorageConfigSpec storageConfigSpec) {
    return storageConfigHandler.createStorageConfig(cUUID, storageConfigSpec);
  }

  @Override
  public StorageConfig getStorageConfig(Http.Request request, UUID cUUID, UUID scUUID) {
    return storageConfigHandler.getStorageConfig(cUUID, scUUID);
  }

  @Override
  public YBATask validateStorageConfig(
      Http.Request request, UUID cUUID, StorageConfigSpec storageConfigSpec) {
    return storageConfigHandler.validateStorageConfig(cUUID, storageConfigSpec);
  }
}
