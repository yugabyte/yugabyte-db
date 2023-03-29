// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.BaseBeanValidator;
import java.util.List;
import java.util.concurrent.TimeUnit;
import org.apache.commons.lang3.StringUtils;

public abstract class ProviderFieldsValidator extends BaseBeanValidator {

  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public ProviderFieldsValidator(BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter) {
    super(beanValidator);
    this.runtimeConfGetter = runtimeConfGetter;
  }

  protected void throwBeanProviderValidatorError(String fieldName, String exceptionMsg) {
    throwBeanValidatorError(fieldName, exceptionMsg, "providerValidation");
  }

  public boolean validateNTPServers(List<String> ntpServers) {
    try {
      int maxNTPServerValidateCount =
          this.runtimeConfGetter.getStaticConf().getInt("yb.provider.validate_ntp_server_count");
      for (int i = 0; i < Math.min(maxNTPServerValidateCount, ntpServers.size()); i++) {
        String ntpServer = ntpServers.get(i);
        if (!StringUtils.isEmpty(ntpServer)) {
          Process process = Runtime.getRuntime().exec("ping -c 1 " + ntpServer);
          process.waitFor(1000L, TimeUnit.MILLISECONDS);
          if (process.exitValue() != 0) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Could not reach ntp server:  " + ntpServer);
          }
        }
      }
    } catch (Exception e) {
      throwBeanProviderValidatorError("NTP_SERVERS", e.getMessage());
    }
    return true;
  }

  public abstract void validate(Provider provider);
}
