// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.DetachUniverseFormData;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.UniverseSpec;
import com.yugabyte.yw.models.UniverseSpec.PlatformPaths;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.Api;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.libs.Files.TemporaryFile;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Result;

@Slf4j
@Api(hidden = true)
public class AttachDetachController extends AuthenticatedController {

  @Inject private Config config;

  @Inject private RuntimeConfGetter confGetter;

  @Inject private ReleaseManager releaseManager;

  @Inject private SwamperHelper swamperHelper;

  private static final String RELEASES_PATH = "yb.releases.path";
  private static final String YBC_RELEASE_PATH = "ybc.docker.release";
  private static final String YBC_RELEASES_PATH = "ybc.releases.path";

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result exportUniverse(UUID customerUUID, UUID universeUUID, Request request)
      throws IOException {
    JsonNode requestBody = request.body().asJson();
    checkAttachDetachEnabled();

    DetachUniverseFormData detachUniverseFormData =
        formFactory.getFormDataOrBadRequest(requestBody, DetachUniverseFormData.class);
    log.debug("Universe spec will include releases: {}", !detachUniverseFormData.skipReleases);

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));

    List<InstanceType> instanceTypes =
        InstanceType.findByProvider(
            provider,
            confGetter,
            confGetter.getConfForScope(provider, ProviderConfKeys.allowUnsupportedInstances));

    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(universe.getUniverseUUID());
    if (!xClusterConfigs.isEmpty()) {
      throw new PlatformServiceException(
          METHOD_NOT_ALLOWED,
          "Detach universe currently does not support universes with xcluster replication set up.");
    }

    // Validate that universe is in a healthy state, not currently updating, or paused.
    if (universe.getUniverseDetails().updateInProgress
        || !universe.getUniverseDetails().updateSucceeded
        || universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Detach universe is not allowed if universe is currently updating, unhealthy, "
                  + "or in paused state. UpdateInProgress = %b, UpdateSucceeded = %b, "
                  + "UniversePaused = %b",
              universe.getUniverseDetails().updateInProgress,
              universe.getUniverseDetails().updateSucceeded,
              universe.getUniverseDetails().universePaused));
    }

    // Lock Universe to prevent updates from happening.
    universe = Util.lockUniverse(universe);
    UniverseSpec universeSpec;
    InputStream is;
    try {
      List<PriceComponent> priceComponents = PriceComponent.findByProvider(provider);

      List<CertificateInfo> certificateInfoList = CertificateInfo.getCertificateInfoList(universe);

      List<KmsHistory> kmsHistoryList =
          EncryptionAtRestUtil.getAllUniverseKeys(universe.getUniverseUUID());
      kmsHistoryList.sort((h1, h2) -> h1.getTimestamp().compareTo(h2.getTimestamp()));
      List<KmsConfig> kmsConfigs =
          kmsHistoryList.stream()
              .map(KmsHistory::getConfigUuid)
              .distinct()
              .map(KmsConfig::get)
              .collect(Collectors.toList());

      List<Backup> backups =
          Backup.fetchByUniverseUUID(customer.getUuid(), universe.getUniverseUUID());
      List<Schedule> schedules =
          Schedule.getAllSchedulesByOwnerUUIDAndType(
              universe.getUniverseUUID(), TaskType.CreateBackup);
      List<CustomerConfig> customerConfigs =
          backups.stream()
              .map(Backup::getStorageConfigUUID)
              .distinct()
              .map(CustomerConfig::get)
              .collect(Collectors.toList());

      // Non-local releases will not be populated by importLocalReleases, so we need to add it
      // ourselves.
      ReleaseContainer release =
          releaseManager.getReleaseByVersion(
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      if (release != null && release.hasLocalRelease()) {
        release = null;
      }

      List<NodeInstance> nodeInstances = NodeInstance.listByUniverse(universe.getUniverseUUID());

      String storagePath = AppConfigHelper.getStoragePath();
      String releasesPath = confGetter.getStaticConf().getString(RELEASES_PATH);
      String ybcReleasePath = confGetter.getStaticConf().getString(YBC_RELEASE_PATH);
      String ybcReleasesPath = confGetter.getStaticConf().getString(YBC_RELEASES_PATH);

      PlatformPaths platformPaths =
          PlatformPaths.builder()
              .storagePath(storagePath)
              .releasesPath(releasesPath)
              .ybcReleasePath(ybcReleasePath)
              .ybcReleasesPath(ybcReleasesPath)
              .build();

      universeSpec =
          UniverseSpec.builder()
              .universe(universe)
              .universeConfig(universe.getConfig())
              .provider(provider)
              .instanceTypes(instanceTypes)
              .priceComponents(priceComponents)
              .certificateInfoList(certificateInfoList)
              .nodeInstances(nodeInstances)
              .kmsHistoryList(kmsHistoryList)
              .kmsConfigs(kmsConfigs)
              .schedules(schedules)
              .backups(backups)
              .customerConfigs(customerConfigs)
              .ybReleaseMetadata(release)
              .oldPlatformPaths(platformPaths)
              .skipReleases(detachUniverseFormData.skipReleases)
              .build();

      is = universeSpec.exportSpec();
    } catch (Exception e) {
      // Unlock the universe if error is thrown to return universe back to original state.
      Util.unlockUniverse(universe);
      throw e;
    }

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universe.getUniverseUUID().toString(),
            Audit.ActionType.Export,
            universeSpec.generateUniverseSpecObj());
    return ok(is)
        .withHeader("Content-Disposition", "attachment; filename=universeSpec.tar.gz")
        .as("application/gzip");
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result importUniverse(UUID customerUUID, UUID universeUUID, Http.Request request)
      throws IOException {
    checkAttachDetachEnabled();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Http.MultipartFormData<TemporaryFile> body = request.body().asMultipartFormData();
    Http.MultipartFormData.FilePart<TemporaryFile> tempSpecFile = body.getFile("spec");

    if (Universe.maybeGet(universeUUID).isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Universe with uuid %s already exists", universeUUID.toString()));
    }

    if (tempSpecFile == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Failed to get uploaded spec file");
    }

    String storagePath = AppConfigHelper.getStoragePath();
    String releasesPath = confGetter.getStaticConf().getString(RELEASES_PATH);
    String ybcReleasePath = confGetter.getStaticConf().getString(YBC_RELEASE_PATH);
    String ybcReleasesPath = confGetter.getStaticConf().getString(YBC_RELEASES_PATH);

    PlatformPaths platformPaths =
        PlatformPaths.builder()
            .storagePath(storagePath)
            .releasesPath(releasesPath)
            .ybcReleasePath(ybcReleasePath)
            .ybcReleasesPath(ybcReleasesPath)
            .build();

    UniverseSpec universeSpec =
        UniverseSpec.importSpec(tempSpecFile.getRef().path(), platformPaths, customer);
    universeSpec.save(platformPaths, releaseManager, swamperHelper);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeSpec.universe.getUniverseUUID().toString(),
            Audit.ActionType.Import);
    return YBPSuccess.empty();
  }

  @Transactional
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteUniverseMetadata(UUID customerUUID, UUID universeUUID, Request request)
      throws IOException {
    checkAttachDetachEnabled();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    List<Schedule> schedules =
        Schedule.getAllSchedulesByOwnerUUIDAndType(
            universe.getUniverseUUID(), TaskType.CreateBackup);

    for (Schedule schedule : schedules) {
      schedule.delete();
    }

    List<Backup> backups =
        Backup.fetchByUniverseUUID(customer.getUuid(), universe.getUniverseUUID());
    for (Backup backup : backups) {
      backup.delete();
    }

    List<KmsHistory> kmsHistoryList =
        EncryptionAtRestUtil.getAllUniverseKeys(universe.getUniverseUUID());

    for (KmsHistory kmsHistory : kmsHistoryList) {
      kmsHistory.delete();
    }

    List<NodeInstance> nodeInstances = NodeInstance.listByUniverse(universe.getUniverseUUID());
    for (NodeInstance nodeInstance : nodeInstances) {
      nodeInstance.delete();
    }

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universe.getUniverseUUID().toString(),
            Audit.ActionType.DeleteMetadata);

    Universe.delete(universe.getUniverseUUID());
    return YBPSuccess.empty();
  }

  public void checkAttachDetachEnabled() {
    boolean attachDetachEnabled = confGetter.getGlobalConf(GlobalConfKeys.attachDetachEnabled);
    if (!attachDetachEnabled) {
      throw new PlatformServiceException(BAD_REQUEST, "Attach/Detach feature is not enabled");
    }
  }
}
