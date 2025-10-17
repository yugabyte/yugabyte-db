package com.yugabyte.yw.models.helpers.telemetry;

import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;

public class TelemetryProviderUtil {
  public static boolean skipConnectivityValidation(RuntimeConfGetter confGetter) {
    return confGetter != null
        && confGetter.getGlobalConf(GlobalConfKeys.telemetrySkipConnectivityValidations);
  }
}
