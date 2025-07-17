// Copyright (c) YugaByte, Inc.

package api.v2.handlers;

import static com.yugabyte.yw.models.helpers.CommonUtils.FIPS_ENABLED;

import api.v2.models.YBAInfo;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import play.mvc.Http;

public class YbaInstanceHandler extends ApiControllerUtils {
  @Inject private Config config;

  public YBAInfo getYBAInstanceInfo(Http.Request request) throws Exception {
    YBAInfo ybaInfo = new YBAInfo();
    ybaInfo.setFipsEnabled(config.getBoolean(FIPS_ENABLED));
    return ybaInfo;
  }
}
