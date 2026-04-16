package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;

@Singleton
public class NodeUIApiHelper extends ApiHelper {

  @Inject
  public NodeUIApiHelper(CustomWsClientFactory wsClientFactory, Config config) {
    super(wsClientFactory.forCustomConfig(config.getValue(Util.YB_NODE_UI_WS_KEY)));
  }
}
