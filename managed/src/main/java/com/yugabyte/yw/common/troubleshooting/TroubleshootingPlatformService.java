/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.troubleshooting;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.TroubleshootingPlatformExt;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.TroubleshootingPlatform;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.TroubleshootingPlatformFilter;
import io.ebean.ExpressionList;
import io.ebean.annotation.Transactional;
import java.util.*;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
@Slf4j
public class TroubleshootingPlatformService {
  private final BeanValidator beanValidator;
  private final TroubleshootingPlatformClient client;
  private final RuntimeConfGetter confGetter;

  @Inject
  public TroubleshootingPlatformService(
      BeanValidator beanValidator,
      TroubleshootingPlatformClient client,
      RuntimeConfGetter confGetter) {
    this.beanValidator = beanValidator;
    this.client = client;
    this.confGetter = confGetter;
  }

  @Transactional
  public TroubleshootingPlatform save(
      TroubleshootingPlatform troubleshootingPlatform, boolean force) {
    boolean isUpdate = false;
    if (troubleshootingPlatform.getUuid() == null) {
      troubleshootingPlatform.generateUUID();
    } else {
      isUpdate = true;
      TroubleshootingPlatform existingConfig =
          getOrBadRequest(
              troubleshootingPlatform.getCustomerUUID(), troubleshootingPlatform.getUuid());
      if (!force && !existingConfig.getTpUrl().equals(troubleshootingPlatform.getTpUrl())) {
        TroubleshootingPlatformExt.InUseStatus inUseStatus = client.getInUseStatus(existingConfig);
        if (inUseStatus == TroubleshootingPlatformExt.InUseStatus.IN_USE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Can't change Troubleshooting Platform URL while platform is in use");
        } else if (inUseStatus == TroubleshootingPlatformExt.InUseStatus.ERROR) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Can't change Troubleshooting Platform URL while old URL is not accessible");
        }
      }
    }

    validate(troubleshootingPlatform);
    client.putCustomerMetadata(troubleshootingPlatform);
    if (isUpdate) {
      troubleshootingPlatform.update();
    } else {
      troubleshootingPlatform.save();
    }
    return troubleshootingPlatform;
  }

  public TroubleshootingPlatform get(UUID customerUuid, UUID uuid) {
    if (uuid == null || customerUuid == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't get Troubleshooting Platform by null uuid");
    }
    TroubleshootingPlatformFilter filter =
        TroubleshootingPlatformFilter.builder().customerUuid(customerUuid).uuid(uuid).build();
    return list(filter).stream().findFirst().orElse(null);
  }

  public TroubleshootingPlatform getOrBadRequest(UUID customerUUID, UUID uuid) {
    TroubleshootingPlatform platform = get(customerUUID, uuid);
    if (platform == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Troubleshooting Platform not found");
    }
    return platform;
  }

  public List<TroubleshootingPlatform> list(TroubleshootingPlatformFilter filter) {
    ExpressionList<TroubleshootingPlatform> query = TroubleshootingPlatform.createQuery();
    if (CollectionUtils.isNotEmpty(filter.getUuids())) {
      query = appendInClause(TroubleshootingPlatform.createQuery(), "uuid", filter.getUuids());
    }
    if (filter.getCustomerUuid() != null) {
      query = query.eq("customerUUID", filter.getCustomerUuid());
    }
    if (filter.getTpUrl() != null) {
      query = query.eq("tpUrl", filter.getTpUrl());
    }
    return query.findList();
  }

  @Transactional
  public void delete(UUID customerUuid, UUID uuid, boolean force) {
    TroubleshootingPlatform platform = getOrBadRequest(customerUuid, uuid);
    try {
      client.deleteCustomerMetadata(platform);
    } catch (PlatformServiceException e) {
      if (!force) {
        throw e;
      }
    }
    platform.delete();
  }

  public TroubleshootingPlatformExt.InUseStatus getInUseStatus(TroubleshootingPlatform platform) {
    return client.getInUseStatus(platform);
  }

  public boolean isRegistered(TroubleshootingPlatform troubleshootingPlatform, Universe universe) {
    return client.getUniverseMetadata(troubleshootingPlatform, universe.getUniverseUUID()) != null;
  }

  public void putUniverse(TroubleshootingPlatform troubleshootingPlatform, Universe universe) {
    TroubleshootingPlatformClient.UniverseMetadata universeMetadata =
        new TroubleshootingPlatformClient.UniverseMetadata()
            .setId(universe.getUniverseUUID())
            .setCustomerId(troubleshootingPlatform.getCustomerUUID())
            .setDataMountPoints(splitMountPoints(MetricQueryHelper.getDataMountPoints(universe)))
            .setOtherMountPoints(
                splitMountPoints(MetricQueryHelper.getOtherMountPoints(confGetter, universe)));
    client.putUniverseMetadata(troubleshootingPlatform, universeMetadata);
  }

  public void deleteUniverse(TroubleshootingPlatform troubleshootingPlatform, Universe universe) {
    client.deleteUniverseMetadata(troubleshootingPlatform, universe.getUniverseUUID());
  }

  private List<String> splitMountPoints(String mountPoints) {
    return Arrays.stream(mountPoints.split("\\|")).toList();
  }

  public void validate(TroubleshootingPlatform platform) {
    beanValidator.validate(platform);

    TroubleshootingPlatformFilter filter =
        TroubleshootingPlatformFilter.builder()
            .customerUuid(platform.getCustomerUUID())
            .tpUrl(platform.getTpUrl())
            .build();
    List<TroubleshootingPlatform> platformWithSameUrl = list(filter);
    if (CollectionUtils.isNotEmpty(platformWithSameUrl)
        && !platformWithSameUrl.get(0).getUuid().equals(platform.getUuid())) {
      beanValidator
          .error()
          .forField("tpUrl", "platform with such url already exists.")
          .throwError();
    }
  }
}
