// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.SetMultimap;
import com.google.inject.Inject;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.BaseBeanValidator;
import java.util.List;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public abstract class ProviderFieldsValidator extends BaseBeanValidator {

  private static final long PROCESS_WAIT_TIMEOUT_MILLIS = 1000L;

  private final RuntimeConfGetter runtimeConfGetter;

  private final String VALIDATION_ERROR_SOURCE = "providerValidation";

  @Inject
  public ProviderFieldsValidator(BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter) {
    super(beanValidator);
    this.runtimeConfGetter = runtimeConfGetter;
  }

  protected void throwBeanProviderValidatorError(
      String fieldName, String exceptionMsg, JsonNode requestJson) {
    throwBeanValidatorError(fieldName, exceptionMsg, VALIDATION_ERROR_SOURCE, requestJson);
  }

  protected void throwMultipleProviderValidatorError(
      SetMultimap<String, String> errorsMap, JsonNode requestJson) {
    throwMultipleBeanValidatorError(errorsMap, VALIDATION_ERROR_SOURCE, requestJson);
  }

  public boolean validateNTPServers(List<String> ntpServers) {
    try {
      int maxNTPServerValidateCount =
          this.runtimeConfGetter.getStaticConf().getInt("yb.provider.validate_ntp_server_count");
      for (int i = 0; i < Math.min(maxNTPServerValidateCount, ntpServers.size()); i++) {
        String ntpServer = ntpServers.get(i);
        if (!StringUtils.isEmpty(ntpServer)) {
          Process process = Runtime.getRuntime().exec("ping -c 1 " + ntpServer);
          process.waitFor(PROCESS_WAIT_TIMEOUT_MILLIS, TimeUnit.MILLISECONDS);
          if (process.exitValue() != 0) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Could not reach ntp server:  " + ntpServer);
          }
        }
      }
    } catch (Exception e) {
      log.error("Error: ", e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    return true;
  }

  public void validatePrivateKey(List<AccessKey> allAccessKeys) {
    for (AccessKey accessKey : allAccessKeys) {
      String privateKeyContent = accessKey.getKeyInfo().sshPrivateKeyContent;
      if (!CertificateHelper.isValidRsaKey(privateKeyContent)) {
        throw new PlatformServiceException(BAD_REQUEST, "Please provide a valid RSA key");
      }
    }
  }

  public void validatePrivateKeys(
      Provider provider, JsonNode processedJson, SetMultimap<String, String> validationErrorsMap) {
    if (provider.getAllAccessKeys() != null && provider.getAllAccessKeys().size() > 0) {
      ArrayNode accessKeysJson = (ArrayNode) processedJson.get("allAccessKeys");
      int keyIndex = 0;
      for (AccessKey accessKey : provider.getAllAccessKeys()) {
        String keyJsonPath =
            accessKeysJson
                .get(keyIndex++)
                .get("keyInfo")
                .get("sshPrivateKeyContent")
                .get("jsonPath")
                .asText();
        String privateKeyContent = accessKey.getKeyInfo().sshPrivateKeyContent;
        if (!CertificateHelper.isValidRsaKey(privateKeyContent)) {
          validationErrorsMap.put(keyJsonPath, "Not a valid RSA key!");
        }
      }
    }
  }

  public abstract void validate(Provider provider);

  public abstract void validate(AvailabilityZone zone);
}
