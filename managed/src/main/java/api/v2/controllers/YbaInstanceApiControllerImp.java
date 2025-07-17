// Copyright (c) YugaByte, Inc.

package api.v2.controllers;

import api.v2.handlers.YbaInstanceHandler;
import api.v2.models.YBAInfo;
import com.google.inject.Inject;
import play.mvc.Http;

public class YbaInstanceApiControllerImp extends YbaInstanceApiControllerImpInterface {
  @Inject private YbaInstanceHandler ybaInstanceHandler;

  @Override
  public YBAInfo getYBAInstanceInfo(Http.Request request) throws Exception {
    return ybaInstanceHandler.getYBAInstanceInfo(request);
  }
}
