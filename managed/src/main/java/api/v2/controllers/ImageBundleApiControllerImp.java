// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.ImageBundleManagementHandler;
import api.v2.models.ImageBundlePagedQuerySpec;
import api.v2.models.ImageBundlePagedResp;
import com.google.inject.Inject;
import java.util.UUID;
import play.mvc.Http.Request;

public class ImageBundleApiControllerImp extends ImageBundleApiControllerImpInterface {

  private final ImageBundleManagementHandler handler;

  @Inject
  public ImageBundleApiControllerImp(ImageBundleManagementHandler handler) {
    this.handler = handler;
  }

  @Override
  public ImageBundlePagedResp pageListImageBundles(
      Request request,
      UUID cUUID,
      UUID providerUUID,
      ImageBundlePagedQuerySpec imageBundlePagedQuerySpec)
      throws Exception {
    return handler.pageListImageBundles(cUUID, providerUUID, imageBundlePagedQuerySpec);
  }
}
