// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class KubernetesProviderValidator extends ProviderFieldsValidator {

  private final RuntimeConfGetter confGetter;

  public KubernetesProviderValidator(
      BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter) {
    super(beanValidator, runtimeConfGetter);
    this.confGetter = runtimeConfGetter;
  }

  @Override
  public void validate(Provider provider) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableK8sProviderValidation)) {
      log.info("Validation is not enabled");
      return;
    }

    log.info("Validation is not implemented, sending mock failure");
    JsonNode providerJson = Json.toJson(provider);
    SetMultimap<String, String> validationErrors = HashMultimap.create();
    validationErrors.put(
        "$.details.cloudInfo.kubernetes.kubernetesImageRegistry", "Image registry has no tags");
    validationErrors.put(
        "$.details.cloudInfo.kubernetes.kubernetesPullSecretContent",
        "Image registry returend 403 unauthorized");
    validationErrors.put(
        "$.regions[0].zones[0].code", "No nodes in the zone for the Kubernetes cluster");
    validationErrors.put(
        "$.regions[0].zones[0].details.cloudInfo.kubernetes.kubeNamespace",
        "Namespace does not exist");
    validationErrors.put(
        "$.regions[0].zones[0].details.cloudInfo.kubernetes.kubernetesStorageClass",
        "waitForFirstConsumer is not enabled for the storageClass");
    validationErrors.put(
        "$.regions[0].zones[0].details.cloudInfo.kubernetes.kubernetesStorageClass",
        "allowVolumeExpansion is not enabled for the storageClass");
    validationErrors.put(
        "$.regions[0].zones[0].details.cloudInfo.kubernetes.kubeConfigContent",
        "Missing permission to get|create|detele on pods");
    validationErrors.put(
        "$.regions[0].zones[0].details.cloudInfo.kubernetes.kubeConfigContent",
        "Missing permission to create on pods/exec");
    validationErrors.put(
        "$.regions[0].zones[0].details.cloudInfo.kubernetes.certManagerClusterIssuer",
        "Specified cluster issuer does not exist");
    throwMultipleProviderValidatorError(validationErrors, providerJson);
  }

  @Override
  public void validate(AvailabilityZone zone) {
    // pass. We don't have any zone only validation.
  }
}
