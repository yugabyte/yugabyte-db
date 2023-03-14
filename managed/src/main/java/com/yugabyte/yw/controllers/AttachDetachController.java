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
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.DetachUniverseFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.UniverseSpec;
import com.yugabyte.yw.models.UniverseSpec.PlatformPaths;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.XClusterConfig;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.api.libs.Files.TemporaryFile;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
public class AttachDetachController extends AbstractPlatformController {

  @Inject private Config config;

  @Inject private ConfigHelper configHelper;

  @Inject private RuntimeConfGetter confGetter;

  @Inject private ReleaseManager releaseManager;

  @Inject private SwamperHelper swamperHelper;

  private static final String STORAGE_PATH = "yb.storage.path";
  private static final String RELEASES_PATH = "yb.releases.path";
  private static final String YBC_RELEASE_PATH = "ybc.docker.release";
  private static final String YBC_RELEASES_PATH = "ybc.releases.path";

  public Result exportUniverse(UUID customerUUID, UUID universeUUID) throws IOException {
    JsonNode requestBody = request().body().asJson();

    DetachUniverseFormData detachUniverseFormData =
        formFactory.getFormDataOrBadRequest(requestBody, DetachUniverseFormData.class);
    log.debug("Universe spec will include releases: {}", !detachUniverseFormData.skipReleases);

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));

    List<InstanceType> instanceTypes =
        InstanceType.findByProvider(
            provider,
            config,
            configHelper,
            confGetter.getConfForScope(provider, ProviderConfKeys.allowUnsupportedInstances));

    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(universe.getUniverseUUID());
    if (!xClusterConfigs.isEmpty()) {
      throw new PlatformServiceException(
          METHOD_NOT_ALLOWED,
          "Detach universe currently does not support universes with xcluster replication set up.");
    }

    // Validate that universe is in a healthy state and not currently updating.
    if (universe.getUniverseDetails().updateInProgress
        || !universe.getUniverseDetails().updateSucceeded) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Detach universe is not allowed if universe is currently updating or unhealthy "
                  + "state. UpdateInProgress =  {}, UpdateSucceeded = {}",
              universe.getUniverseDetails().updateInProgress,
              universe.getUniverseDetails().updateSucceeded));
    }

    List<PriceComponent> priceComponents = PriceComponent.findByProvider(provider);

    List<CertificateInfo> certificateInfoList = CertificateInfo.getCertificateInfoList(universe);

    List<KmsHistory> kmsHistoryList =
        EncryptionAtRestUtil.getAllUniverseKeys(universe.universeUUID);
    kmsHistoryList.sort((h1, h2) -> h1.timestamp.compareTo(h2.timestamp));
    List<KmsConfig> kmsConfigs =
        kmsHistoryList
            .stream()
            .map(kmsHistory -> kmsHistory.configUuid)
            .distinct()
            .map(c -> KmsConfig.get(c))
            .collect(Collectors.toList());

    List<Backup> backups = Backup.fetchByUniverseUUID(customer.getUuid(), universe.universeUUID);
    List<Schedule> schedules =
        Schedule.getAllSchedulesByOwnerUUIDAndType(universe.universeUUID, TaskType.CreateBackup);
    List<CustomerConfig> customerConfigs =
        backups
            .stream()
            .map(backup -> backup.storageConfigUUID)
            .distinct()
            .map(ccUUID -> CustomerConfig.get(ccUUID))
            .collect(Collectors.toList());

    // Non-local releases will no be populated by importLocalReleases, so we need to add it
    // ourselves.
    ReleaseMetadata ybReleaseMetadata =
        releaseManager.getReleaseByVersion(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (ybReleaseMetadata != null && ybReleaseMetadata.isLocalRelease()) {
      ybReleaseMetadata = null;
    }

    String storagePath = confGetter.getStaticConf().getString(STORAGE_PATH);
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
        UniverseSpec.builder()
            .universe(universe)
            .universeConfig(universe.getConfig())
            .provider(provider)
            .instanceTypes(instanceTypes)
            .priceComponents(priceComponents)
            .certificateInfoList(certificateInfoList)
            .kmsHistoryList(kmsHistoryList)
            .kmsConfigs(kmsConfigs)
            .schedules(schedules)
            .backups(backups)
            .customerConfigs(customerConfigs)
            .ybReleaseMetadata(ybReleaseMetadata)
            .oldPlatformPaths(platformPaths)
            .skipReleases(detachUniverseFormData.skipReleases)
            .build();

    InputStream is = universeSpec.exportSpec();

    response().setHeader("Content-Disposition", "attachment; filename=universeSpec.tar.gz");
    return ok(is).as("application/gzip");
  }

  public Result importUniverse(UUID customerUUID, UUID universeUUID) throws IOException {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Http.MultipartFormData<TemporaryFile> body = request().body().asMultipartFormData();
    Http.MultipartFormData.FilePart<TemporaryFile> tempSpecFile = body.getFile("spec");

    if (Universe.maybeGet(universeUUID).isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Universe with uuid %s already exists", universeUUID.toString()));
    }

    if (tempSpecFile == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Failed to get uploaded spec file");
    }

    String storagePath = confGetter.getStaticConf().getString(STORAGE_PATH);
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

    File tempFile = (File) tempSpecFile.getFile();
    UniverseSpec universeSpec = UniverseSpec.importSpec(tempFile, platformPaths, customer);
    universeSpec.save(platformPaths, releaseManager, swamperHelper);
    return ok();
  }

  public Result deleteUniverseMetadata(UUID customerUUID, UUID universeUUID) throws IOException {
    // TODO: Soft delete stub
    return ok();
  }
}
