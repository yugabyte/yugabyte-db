// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.oci.OCICloudImpl;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
@Singleton
public class OCIProviderValidator extends ProviderFieldsValidator {

  private final OCICloudImpl ociCloudImpl;
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public OCIProviderValidator(
      BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter, OCICloudImpl ociCloudImpl) {
    super(beanValidator, runtimeConfGetter);
    this.runtimeConfGetter = runtimeConfGetter;
    this.ociCloudImpl = ociCloudImpl;
  }

  @Override
  public void validate(Provider provider) {
    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableOciProviderValidation)) {
      log.warn("OCI provider validation is not enabled");
      return;
    }

    if (!ociCloudImpl.isValidCreds(provider)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid OCI credentials or configuration: ensure compartment and region are set,"
              + " and API key or instance principal auth is configured correctly [check logs for"
              + " details]");
    }

    SetMultimap<String, String> validationErrorsMap = HashMultimap.create();

    List<AccessKey> accessKeys = provider.getAllAccessKeys();
    if (CollectionUtils.isNotEmpty(accessKeys)) {
      try {
        validatePrivateKey(accessKeys);
      } catch (PlatformServiceException e) {
        if (e.getHttpStatus() == BAD_REQUEST) {
          validationErrorsMap.put("SSH_PRIVATE_KEY_CONTENT", e.getMessage());
        } else {
          throw e;
        }
      }
    }

    if (provider.getDetails() != null && provider.getDetails().ntpServers != null) {
      try {
        validateNTPServers(provider.getDetails().ntpServers);
      } catch (PlatformServiceException e) {
        validationErrorsMap.put("NTP_SERVERS", e.getMessage());
      }
    }

    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, null);
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    // No OCI-specific availability zone payload checks beyond shared AZ rules.
  }
}
