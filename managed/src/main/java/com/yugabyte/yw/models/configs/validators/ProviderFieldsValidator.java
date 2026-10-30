// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.SetMultimap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.BaseBeanValidator;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public abstract class ProviderFieldsValidator extends BaseBeanValidator {

  private static final long PROCESS_WAIT_TIMEOUT_MILLIS = 5000L;

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

  /** Storage clouds federation can currently reach. */
  private static final Set<CloudType> SUPPORTED_TARGET_CLOUDS =
      ImmutableSet.of(CloudType.aws, CloudType.gcp);

  /**
   * Validates every configured cross-cloud federation target on its own: a provider may carry
   * several, and having one usable entry does not excuse a malformed other. Shared by every
   * provider type so the same payload is accepted or rejected identically whichever cloud it is
   * attached to. Validating at save keeps a malformed value from failing later on every node in
   * ManageCloudFederation.
   *
   * <p>Reads the raw configured targets rather than {@link
   * CloudInfoInterface#getCrossCloudFederationTargets}, which filters unusable entries out - that
   * is exactly what has to be reported here.
   */
  protected void validateCrossCloudFederationTargets(Provider provider) {
    CloudInfoInterface info = CloudInfoInterface.get(provider);
    if (info == null || !info.isFederatedIamEnabled()) {
      return;
    }
    List<CrossCloudFederationTarget> targets = info.getEffectiveFederationTargets();
    if (targets.isEmpty()) {
      throwBeanProviderValidatorError(
          "FEDERATED_IAM",
          "Federated IAM is enabled but no target is configured. Add a crossCloudFederationTargets"
              + " entry per storage cloud this provider's nodes must reach.",
          null);
    }
    Set<CloudType> seen = new HashSet<>();
    for (CrossCloudFederationTarget target : targets) {
      if (target.targetCloud == null || !SUPPORTED_TARGET_CLOUDS.contains(target.targetCloud)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_TARGET_CLOUD",
            String.format(
                "Unsupported federated IAM target cloud %s. Supported: %s.",
                target.targetCloud, SUPPORTED_TARGET_CLOUDS),
            null);
      }
      // One entry per cloud: two would make "the settings for reaching S3" ambiguous, and the
      // node picks its entry by target cloud alone.
      if (!seen.add(target.targetCloud)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_TARGET_CLOUD",
            String.format("Duplicate federated IAM target for cloud %s.", target.targetCloud),
            null);
      }
      if (!CrossCloudFederationTarget.isValidAudience(target.audience)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_AUDIENCE",
            String.format(
                "Federated IAM audience for target %s is missing or contains unsupported"
                    + " characters.",
                target.targetCloud),
            null);
      }
      // Reaching S3 is only possible with a role to assume, so a half-configured entry is an
      // error rather than something to quietly ignore.
      if (target.targetCloud == CloudType.aws
          && !CrossCloudFederationTarget.isValidRoleArn(target.roleArn)) {
        throwBeanProviderValidatorError(
            "FEDERATED_IAM_ROLE_ARN",
            "Federated IAM role ARN for the aws target is missing or malformed. Expected"
                + " arn:aws:iam::<12-digit-account>:role/<role-name>.",
            null);
      }
    }
  }

  public boolean validateNTPServers(List<String> ntpServers) {
    try {
      int maxNTPServerValidateCount =
          this.runtimeConfGetter.getStaticConf().getInt("yb.provider.validate_ntp_server_count");
      for (int i = 0; i < Math.min(maxNTPServerValidateCount, ntpServers.size()); i++) {
        String ntpServer = ntpServers.get(i);
        if (!StringUtils.isEmpty(ntpServer)) {
          Process process = Runtime.getRuntime().exec("nc -zvu " + ntpServer + " 123");
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
