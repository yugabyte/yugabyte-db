/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.pa;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.PACollectorExt;
import com.yugabyte.yw.forms.PaUniverseInfo;
import com.yugabyte.yw.forms.PaUniverseInfo.SortBy;
import com.yugabyte.yw.forms.paging.PaUniverseApiFilter;
import com.yugabyte.yw.forms.paging.PaUniversePagedApiQuery;
import com.yugabyte.yw.forms.paging.PaUniversePagedApiResponse;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.ExpressionList;
import io.ebean.annotation.Transactional;
import java.util.*;
import java.util.stream.Stream;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class PerfAdvisorService {
  private final BeanValidator beanValidator;
  private final PerfAdvisorClient client;
  private final RuntimeConfGetter confGetter;
  private final SettableRuntimeConfigFactory configFactory;

  @Inject
  public PerfAdvisorService(
      BeanValidator beanValidator,
      PerfAdvisorClient client,
      RuntimeConfGetter confGetter,
      SettableRuntimeConfigFactory configFactory) {
    this.beanValidator = beanValidator;
    this.client = client;
    this.confGetter = confGetter;
    this.configFactory = configFactory;
  }

  @Transactional
  public PACollector save(PACollector paCollector, boolean force) {
    boolean isUpdate = false;
    if (paCollector.getUuid() == null) {
      paCollector.generateUUID();
    } else {
      isUpdate = true;
      PACollector existingConfig =
          getOrBadRequest(paCollector.getCustomerUUID(), paCollector.getUuid());
      if (!force && !existingConfig.getPaUrl().equals(paCollector.getPaUrl())) {
        PACollectorExt.InUseStatus inUseStatus = client.getInUseStatus(existingConfig);
        if (inUseStatus == PACollectorExt.InUseStatus.IN_USE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Can't change PA Collector URL while collector is in use");
        } else if (inUseStatus == PACollectorExt.InUseStatus.ERROR) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Can't change PA Collector URL while old URL is not accessible");
        }
      }
    }

    validate(paCollector);
    paCollector.setPaUrl(normalizeUrl(paCollector.getPaUrl()));
    paCollector.setMetricsUrl(normalizeUrl(paCollector.getMetricsUrl()));
    paCollector.setYbaUrl(normalizeUrl(paCollector.getYbaUrl()));
    client.putCustomerMetadata(paCollector);
    if (isUpdate) {
      paCollector.update();
    } else {
      paCollector.save();
    }
    return paCollector;
  }

  public PACollector create(PACollector paCollector) {
    validate(paCollector);
    paCollector.setPaUrl(normalizeUrl(paCollector.getPaUrl()));
    paCollector.setMetricsUrl(normalizeUrl(paCollector.getMetricsUrl()));
    paCollector.setYbaUrl(normalizeUrl(paCollector.getYbaUrl()));
    client.putCustomerMetadata(paCollector);
    paCollector.save();
    return paCollector;
  }

  public PACollector get(UUID customerUuid, UUID uuid) {
    if (uuid == null || customerUuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't get PA Collector by null uuid");
    }
    PACollectorFilter filter =
        PACollectorFilter.builder().customerUuid(customerUuid).uuid(uuid).build();
    return list(filter).stream().findFirst().orElse(null);
  }

  public PACollector getOrBadRequest(UUID customerUUID, UUID uuid) {
    PACollector platform = get(customerUUID, uuid);
    if (platform == null) {
      throw new PlatformServiceException(BAD_REQUEST, "PA Collector not found");
    }
    return platform;
  }

  public List<PACollector> list(PACollectorFilter filter) {
    ExpressionList<PACollector> query = PACollector.createQuery();
    if (CollectionUtils.isNotEmpty(filter.getUuids())) {
      query = appendInClause(PACollector.createQuery(), "uuid", filter.getUuids());
    }
    if (filter.getCustomerUuid() != null) {
      query = query.eq("customerUUID", filter.getCustomerUuid());
    }
    if (filter.getPaUrl() != null) {
      query = query.eq("paUrl", filter.getPaUrl());
    }
    return query.findList();
  }

  @Transactional
  public void delete(UUID customerUuid, UUID uuid, boolean force) {
    PACollector platform = getOrBadRequest(customerUuid, uuid);
    try {
      client.deleteCustomerMetadata(platform);
    } catch (PlatformServiceException e) {
      if (!force) {
        throw e;
      }
    }
    platform.delete();
  }

  public PACollectorExt.InUseStatus getInUseStatus(PACollector platform) {
    return client.getInUseStatus(platform);
  }

  public boolean isRegistered(PACollector paCollector, Universe universe) {
    return client.getUniverseMetadata(paCollector, universe.getUniverseUUID()) != null;
  }

  public PerfAdvisorClient.UniverseMetadata getUniverseMetadata(
      PACollector paCollector, Universe universe) {
    return client.getUniverseMetadata(paCollector, universe.getUniverseUUID());
  }

  public List<PerfAdvisorClient.UniverseMetadata> listRegisteredUniverses(PACollector collector) {
    return client.listUniverseMetadata(collector);
  }

  public PaUniversePagedApiResponse pagedListRegisteredUniverses(
      PACollector collector, PaUniversePagedApiQuery apiQuery) {
    List<PerfAdvisorClient.UniverseMetadata> allMetadata = listRegisteredUniverses(collector);

    Stream<PaUniverseInfo> infoStream =
        allMetadata.stream()
            .map(
                meta -> {
                  PaUniverseInfo info = new PaUniverseInfo();
                  info.setUniverseUuid(meta.getId());
                  Optional<Universe> universe = Universe.maybeGet(meta.getId());
                  info.setUniverseName(universe.map(Universe::getName).orElse(null));
                  info.setDataMountPoints(meta.getDataMountPoints());
                  info.setOtherMountPoints(meta.getOtherMountPoints());
                  info.setAdvancedObservability(meta.isMetricsExportToPrometheusEnabled());
                  return info;
                });

    PaUniverseApiFilter filter = apiQuery.getFilter();
    if (filter != null && StringUtils.isNotEmpty(filter.getUniverseName())) {
      String nameFilter = filter.getUniverseName().toLowerCase();
      infoStream =
          infoStream.filter(
              i ->
                  i.getUniverseName() != null
                      && i.getUniverseName().toLowerCase().contains(nameFilter));
    }

    Comparator<PaUniverseInfo> comparator =
        Comparator.comparing(
            i -> {
              if (apiQuery.getSortBy() == SortBy.universeUuid) {
                return i.getUniverseUuid().toString();
              }
              return i.getUniverseName();
            });
    if (apiQuery.getDirection() == SortDirection.DESC) {
      comparator = comparator.reversed();
    }

    List<PaUniverseInfo> sorted = infoStream.sorted(comparator).toList();

    int totalCount = sorted.size();
    int offset = apiQuery.getOffset();
    int limit = apiQuery.getLimit();
    int fromIndex = Math.min(offset, totalCount);
    int toIndex = Math.min(offset + limit, totalCount);

    PaUniversePagedApiResponse response = new PaUniversePagedApiResponse();
    response.setEntities(sorted.subList(fromIndex, toIndex));
    response.setHasPrev(offset > 0);
    response.setHasNext(toIndex < totalCount);
    response.setTotalCount(totalCount);
    return response;
  }

  public void putUniverse(
      PACollector paCollector, Universe universe, boolean advancedObservability) {
    RuntimeConfig<Universe> runtimeConfig = configFactory.forUniverse(universe);

    boolean dbQueryApiEnabled =
        runtimeConfig.getBoolean(UniverseConfKeys.enableDbQueryApi.getKey());
    if (!dbQueryApiEnabled) {
      log.info(
          "Enabling {} for universe {}",
          UniverseConfKeys.enableDbQueryApi.getKey(),
          universe.getUniverseUUID());
      runtimeConfig.setValue(UniverseConfKeys.enableDbQueryApi.getKey(), Boolean.TRUE.toString());
    }

    PerfAdvisorClient.UniverseMetadata universeMetadata =
        new PerfAdvisorClient.UniverseMetadata()
            .setId(universe.getUniverseUUID())
            .setCustomerId(paCollector.getCustomerUUID())
            .setDataMountPoints(splitMountPoints(MetricQueryHelper.getDataMountPoints(universe)))
            .setOtherMountPoints(
                splitMountPoints(MetricQueryHelper.getOtherMountPoints(confGetter, universe)))
            .setMetricsExportToPrometheusEnabled(advancedObservability);
    client.putUniverseMetadata(paCollector, universeMetadata);
  }

  public void deleteUniverse(PACollector paCollector, Universe universe) {
    client.deleteUniverseMetadata(paCollector, universe.getUniverseUUID());
  }

  private List<String> splitMountPoints(String mountPoints) {
    return Arrays.stream(mountPoints.split("\\|")).toList();
  }

  public void validate(PACollector platform) {
    beanValidator.validate(platform);

    PACollectorFilter filter =
        PACollectorFilter.builder()
            .customerUuid(platform.getCustomerUUID())
            .paUrl(platform.getPaUrl())
            .build();
    List<PACollector> platformWithSameUrl = list(filter);
    if (CollectionUtils.isNotEmpty(platformWithSameUrl)
        && !platformWithSameUrl.get(0).getUuid().equals(platform.getUuid())) {
      beanValidator
          .error()
          .forField("paUrl", "collector with such url already exists.")
          .throwError();
    }
  }

  private String normalizeUrl(String url) {
    if (url.endsWith("/")) {
      return url.substring(0, url.length() - 1);
    }
    return url;
  }
}
