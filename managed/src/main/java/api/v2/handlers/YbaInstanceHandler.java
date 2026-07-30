// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static com.yugabyte.yw.models.helpers.CommonUtils.FIPS_ENABLED;

import api.v2.models.YBAInfo;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.models.Customer;

public class YbaInstanceHandler extends ApiControllerUtils {
  private final Config config;

  @Inject
  public YbaInstanceHandler(AuditService auditService, Config config) {
    super(auditService);
    this.config = config;
  }

  public YBAInfo getYBAInstanceInfo() {
    YBAInfo ybaInfo = new YBAInfo();
    ybaInfo.setFipsEnabled(config.getBoolean(FIPS_ENABLED));
    ybaInfo.setCustomerCount(Customer.find.query().findCount());
    ybaInfo.setVersion(Util.getYbaVersion());
    return ybaInfo;
  }
}
