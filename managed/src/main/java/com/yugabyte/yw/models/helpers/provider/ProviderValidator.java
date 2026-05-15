// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.provider;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.cloud.gcp.GCPProjectApiClientFactory;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.configs.validators.AWSProviderValidator;
import com.yugabyte.yw.models.configs.validators.AzureProviderValidator;
import com.yugabyte.yw.models.configs.validators.GCPProviderValidator;
import com.yugabyte.yw.models.configs.validators.KubernetesProviderValidator;
import com.yugabyte.yw.models.configs.validators.OnPremValidator;
import com.yugabyte.yw.models.configs.validators.ProviderFieldsValidator;
import com.yugabyte.yw.models.helpers.BaseBeanValidator;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class ProviderValidator extends BaseBeanValidator {

  private final Map<String, ProviderFieldsValidator> providerValidatorMap = new HashMap<>();
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public ProviderValidator(
      BeanValidator beanValidator,
      AWSCloudImpl awsCloudImpl,
      GCPCloudImpl gcpCloudImpl,
      KubernetesManagerFactory kubernetesManagerFactory,
      GCPProjectApiClientFactory gcpClientFactory,
      RuntimeConfGetter runtimeConfGetter,
      ConfigHelper configHelper) {
    super(beanValidator);
    this.runtimeConfGetter = runtimeConfGetter;
    this.providerValidatorMap.put(
        CloudType.aws.toString(),
        new AWSProviderValidator(beanValidator, awsCloudImpl, runtimeConfGetter));
    this.providerValidatorMap.put(
        CloudType.onprem.toString(), new OnPremValidator(beanValidator, runtimeConfGetter));
    this.providerValidatorMap.put(
        CloudType.kubernetes.toString(),
        new KubernetesProviderValidator(
            beanValidator, runtimeConfGetter, kubernetesManagerFactory));
    this.providerValidatorMap.put(
        CloudType.azu.toString(),
        new AzureProviderValidator(runtimeConfGetter, beanValidator, configHelper));
    this.providerValidatorMap.put(
        CloudType.gcp.toString(),
        new GCPProviderValidator(beanValidator, runtimeConfGetter, gcpCloudImpl, gcpClientFactory));
  }

  public void validateAvailabiltyZone(
      Provider requestedProvider, @Nullable Provider existingProvider) {
    if (CollectionUtils.isEmpty(requestedProvider.getRegions())) {
      log.debug("Skipping AZ validation because there are no regions specified");
      return;
    }
    boolean allowExistingDuplicateAz =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.allowExistingDuplicateAz);
    List<AvailabilityZone> requestedZones =
        requestedProvider.getRegions().stream()
            .filter(r -> CollectionUtils.isNotEmpty(r.getZones()))
            .flatMap(r -> r.getZones().stream())
            .collect(Collectors.toList());
    // Get all the existing zones from the existing provider.
    Map<UUID, AvailabilityZone> existingZones =
        existingProvider == null
            ? new HashMap<>()
            : existingProvider.getAllRegions().stream()
                .flatMap(r -> r.getAllZones().stream())
                .collect(Collectors.toMap(AvailabilityZone::getUuid, Function.identity()));
    List<AvailabilityZone> modifiedOrAddedZones = new ArrayList<>();
    Set<String> requestedAddedZoneCodes = new HashSet<>();
    for (AvailabilityZone az : requestedZones) {
      if (allowExistingDuplicateAz) {
        // Existing AZ duplicates are allowed, but newly added AZs must be unique.
        // AZ UUID is needed to detect if it is new or old.
        AvailabilityZone existingAz = az.getUuid() == null ? null : existingZones.get(az.getUuid());
        if (existingAz == null) {
          // AZ is added.
          modifiedOrAddedZones.add(az);
        } else if (!existingAz.getCode().equals(az.getCode())) {
          // AZ code is modified.
          modifiedOrAddedZones.add(az);
        }
      } else {
        // No duplicate is allowed at all.
        if (requestedAddedZoneCodes.contains(az.getCode())) {
          String errMsg =
              String.format(
                  "Duplicate AZ code %s. AZ code must be unique for a provider", az.getCode());
          log.error(errMsg);
          throw new PlatformServiceException(BAD_REQUEST, errMsg);
        } else {
          requestedAddedZoneCodes.add(az.getCode());
        }
      }
    }
    validateAvailabiltyZone(modifiedOrAddedZones, existingProvider);
  }

  public void validateAvailabiltyZone(
      List<AvailabilityZone> modifiedOrAddedZones, @Nullable Provider existingProvider) {
    if (CollectionUtils.isEmpty(modifiedOrAddedZones)) {
      log.debug("Skipping AZ validation because there are no new or modified zones");
      return;
    }
    // This can be called in transaction after inserting the newly added AZ.
    // In that case, a newly added AZ has UUID set.
    Set<UUID> modifiedOrAddedZoneUuids =
        modifiedOrAddedZones.stream()
            .map(AvailabilityZone::getUuid)
            .filter(Objects::nonNull)
            .collect(Collectors.toSet());
    // Run the check for unique zone code only on new zones for backward compatibility.
    // Remove the overlapping the modified or added zones because they may already be saved in a
    // transaction in the same call.
    Set<String> existingZoneCodes =
        existingProvider == null
            ? new HashSet<>()
            : existingProvider.getAllRegions().stream()
                .flatMap(r -> r.getAllZones().stream())
                .filter(z -> z.isActive()) // Excluding deleted (they are handled)
                .filter(az -> !modifiedOrAddedZoneUuids.contains(az.getUuid()))
                .map(AvailabilityZone::getCode)
                .collect(Collectors.toCollection(HashSet::new));
    for (AvailabilityZone newZone : modifiedOrAddedZones) {
      if (existingZoneCodes.contains(newZone.getCode())) {
        String errMsg =
            String.format(
                "Duplicate AZ code %s. AZ code must be unique for a provider. Make sure to set"
                    + " 'uuid' field for the existing AZs",
                newZone.getCode());
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
      existingZoneCodes.add(newZone.getCode());
    }
  }

  public void validate(Provider requestedProvider, @Nullable Provider existingProvider) {
    try {
      validateAvailabiltyZone(requestedProvider, existingProvider);
      ProviderFieldsValidator providerFieldsValidator =
          providerValidatorMap.get(requestedProvider.getCode());
      if (providerFieldsValidator != null) {
        providerFieldsValidator.validate(requestedProvider);
      }
    } catch (RuntimeException e) {
      log.error("Failed to validate provider payload", e);
      if (!(e instanceof PlatformServiceException)) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Failed to validate provider payload. Please check for malformed or incorrect fields");
      }
      throw e;
    }
  }

  public void validate(AvailabilityZone zone, String providerCode) {
    validateAvailabiltyZone(Collections.singletonList(zone), zone.getProvider());
    ProviderFieldsValidator providerFieldsValidator = providerValidatorMap.get(providerCode);
    if (providerFieldsValidator != null) {
      providerFieldsValidator.validate(zone);
    }
  }
}
