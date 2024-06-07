// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.ReleaseFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Release management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class ReleaseController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(ReleaseController.class);

  @Inject ReleaseManager releaseManager;

  @Inject GFlagsValidation gFlagsValidation;

  @Inject ValidatingFormFactory formFactory;

  @Inject RuntimeConfGetter confGetter;

  @Inject ReleasesUtils releasesUtils;

  @Deprecated
  @ApiOperation(
      value =
          "Deprecated: sinceVersion 2024.1. Use ReleasesController.create instead. Create a"
              + " release",
      response = YBPSuccess.class,
      nickname = "createRelease")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Release",
        value = "Release data for remote downloading to be created",
        required = true,
        dataType = "com.yugabyte.yw.forms.ReleaseFormData",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result create(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    Iterator<Map.Entry<String, JsonNode>> it = request.body().asJson().fields();
    List<ReleaseFormData> versionDataList = new ArrayList<>();
    while (it.hasNext()) {
      Map.Entry<String, JsonNode> versionJson = it.next();
      ReleaseFormData formData =
          formFactory.getFormDataOrBadRequest(versionJson.getValue(), ReleaseFormData.class);
      formData.version = versionJson.getKey();
      if (!Util.isYbVersionFormatValid(formData.version)) {
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("Version %s is not valid", formData.version));
      }
      LOG.info("Asked to add new release: {} ", formData.version);
      versionDataList.add(formData);
    }

    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      // Validate the version
      LOG.warn("creating new style release with legacy api");
      versionDataList.forEach(
          data -> {
            // Validate the version
            releasesUtils.validateVersionAgainstCurrentYBA(data.version);
            if (Release.getByVersion(data.version) != null) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Release " + data.version + "already exists");
            }
            Release release = null;
            List<ReleaseArtifact> artifacts = new ArrayList<>();
            if (data.s3 != null) {
              ReleasesUtils.ExtractedMetadata metadata =
                  releasesUtils.metadataFromName(data.s3.paths.x86_64);
              release = Release.create(data.version, metadata.release_type);
              ReleaseArtifact.S3File s3File = new ReleaseArtifact.S3File();
              s3File.accessKeyId = data.s3.accessKeyId;
              s3File.secretAccessKey = data.s3.secretAccessKey;
              s3File.path = data.s3.paths.x86_64;
              artifacts.add(
                  ReleaseArtifact.create(
                      data.s3.paths.x86_64_checksum,
                      metadata.platform,
                      metadata.architecture,
                      s3File));
              if (data.s3.paths.helmChart != null && !data.s3.paths.helmChart.isEmpty()) {
                s3File.path = data.s3.paths.helmChart;
                artifacts.add(
                    ReleaseArtifact.create(
                        data.s3.paths.helmChartChecksum,
                        ReleaseArtifact.Platform.KUBERNETES,
                        null,
                        s3File));
              }
            } else if (data.gcs != null) {
              ReleasesUtils.ExtractedMetadata metadata =
                  releasesUtils.metadataFromName(data.gcs.paths.x86_64);
              release = Release.create(data.version, metadata.release_type);
              ReleaseArtifact.GCSFile gcsFile = new ReleaseArtifact.GCSFile();
              gcsFile.credentialsJson = data.gcs.credentialsJson;
              gcsFile.path = data.gcs.paths.x86_64;
              artifacts.add(
                  ReleaseArtifact.create(
                      data.gcs.paths.x86_64_checksum,
                      metadata.platform,
                      metadata.architecture,
                      gcsFile));
              if (data.gcs.paths.helmChart != null && !data.gcs.paths.helmChart.isEmpty()) {
                gcsFile.path = data.gcs.paths.helmChart;
                artifacts.add(
                    ReleaseArtifact.create(
                        data.gcs.paths.helmChartChecksum,
                        ReleaseArtifact.Platform.KUBERNETES,
                        null,
                        gcsFile));
              }
            } else if (data.http != null) {
              ReleasesUtils.ExtractedMetadata metadata =
                  releasesUtils.metadataFromName(data.http.paths.x86_64);
              release = Release.create(data.version, metadata.release_type);
              artifacts.add(
                  ReleaseArtifact.create(
                      data.http.paths.x86_64_checksum,
                      metadata.platform,
                      metadata.architecture,
                      data.http.paths.x86_64));
              if (data.http.paths.helmChart != null && !data.http.paths.helmChart.isEmpty()) {
                artifacts.add(
                    ReleaseArtifact.create(
                        data.http.paths.helmChartChecksum,
                        ReleaseArtifact.Platform.KUBERNETES,
                        null,
                        data.http.paths.helmChart));
              }
            } else {
              throw new PlatformServiceException(BAD_REQUEST, "no paths found in request");
            }

            for (ReleaseArtifact artifact : artifacts) {
              release.addArtifact(artifact);
            }
          });
    } else {
      try {
        Map<String, ReleaseMetadata> releases =
            ReleaseManager.formDataToReleaseMetadata(versionDataList);
        releases.forEach(
            (version, metadata) -> {
              releaseManager.addReleaseWithMetadata(version, metadata);
              gFlagsValidation.addDBMetadataFiles(version);
            });
        releaseManager.updateCurrentReleases();
      } catch (RuntimeException re) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, re.getMessage());
      }
    }

    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Release, versionDataList.toString(), Audit.ActionType.Create);
    return YBPSuccess.empty();
  }

  @Deprecated
  @ApiOperation(
      value =
          "Deprecated: sinceVersion: 2024.1. Use ReleasesController.list instead. List all"
              + " releases",
      response = Object.class,
      responseContainer = "Map",
      nickname = "getListOfReleases")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID, Boolean includeMetadata, @Nullable String arch) {
    Customer.getOrBadRequest(customerUUID);
    Map<String, Object> filtered = null;
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      List<Release> releases = Release.getAll();
      filtered =
          releases.stream()
              .filter(r -> !r.getState().equals(Release.ReleaseState.DELETED))
              .filter(
                  r -> {
                    if (arch == null) return true;
                    return r.getArtifacts().stream()
                        .anyMatch(a -> a.getArchitecture().name().equals(arch));
                  })
              .collect(
                  Collectors.toMap(
                      entry -> entry.getVersion(),
                      entry -> releasesUtils.releaseToReleaseMetadata(entry)));
    } else {
      Map<String, Object> releases = releaseManager.getReleaseMetadata();

      // Filter out any deleted releases.
      filtered =
          releases.entrySet().stream()
              .filter(f -> !Json.toJson(f.getValue()).get("state").asText().equals("DELETED"))
              .filter(
                  f -> {
                    if (arch != null) {
                      return releaseManager
                          .metadataFromObject(f.getValue())
                          .matchesArchitecture(Architecture.valueOf(arch));
                    }
                    return true;
                  })
              .collect(
                  Collectors.toMap(
                      Entry::getKey, entry -> CommonUtils.maskObject(entry.getValue())));
    }
    return PlatformResults.withData(includeMetadata ? filtered : filtered.keySet());
  }

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
  @ApiOperation(
      value = "List releases by provider - deprecated",
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.20.0.0.</b></p>"
              + "Use /api/v1/customers/{cUUID}/releases/:arch instead",
      response = Object.class,
      responseContainer = "Map",
      nickname = "getListOfRegionReleases")
  @Deprecated
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listByProvider(UUID customerUUID, UUID providerUUID, Boolean includeMetadata) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);
    List<Region> regionList = Region.getByProvider(providerUUID);
    if (CollectionUtils.isEmpty(regionList) || regionList.get(0) == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No Regions configured for provider.");
    }
    Region region = regionList.get(0);
    Map<String, Object> releases = releaseManager.getReleaseMetadata();
    Architecture arch = region.getArchitecture();
    // Old region without architecture. Return all releases.
    if (arch == null) {
      LOG.info(
          "ReleaseController: Could not determine region {} architecture. Listing all releases.",
          region.getCode());
      return list(customerUUID, includeMetadata, null);
    }

    // Filter for active and matching region releases.
    Map<String, Object> filtered =
        releases.entrySet().stream()
            .filter(f -> !Json.toJson(f.getValue()).get("state").asText().equals("DELETED"))
            .filter(f -> releaseManager.metadataFromObject(f.getValue()).matchesRegion(region))
            .collect(
                Collectors.toMap(Entry::getKey, entry -> CommonUtils.maskObject(entry.getValue())));
    return PlatformResults.withData(includeMetadata ? filtered : filtered.keySet());
  }

  @Deprecated
  @ApiOperation(
      value =
          "Deprecated: sinceVersion: 2024.1. Use ReleasesController.update instead. Update a"
              + " release",
      response = ReleaseManager.ReleaseMetadata.class,
      nickname = "updateRelease")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Release",
        value = "Release data to be updated",
        required = true,
        dataType = "Object",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result update(UUID customerUUID, String version, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    ObjectNode formData;
    ReleaseContainer release = releaseManager.getReleaseByVersion(version);
    if (release == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Release version: " + version);
    }
    formData = (ObjectNode) request.body().asJson();

    // For now we would only let the user change the state on their releases.
    if (formData.has("state")) {
      String stateValue = formData.get("state").asText();
      LOG.info("Updating release state for version {} to {}", version, stateValue);
      release.setState(stateValue);
      if (release.isLegacy()) {
        releaseManager.updateReleaseMetadata(version, release.getMetadata());
      }
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Missing Required param: State");
    }
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Release, version, Audit.ActionType.Update);
    return PlatformResults.withData(release.getMetadata());
  }

  @ApiOperation(value = "Refresh a release", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result refresh(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    LOG.info("ReleaseController: refresh");
    try {
      releaseManager.importLocalReleases();
      releaseManager.updateCurrentReleases();
    } catch (RuntimeException re) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, re.getMessage());
    }
    auditService()
        .createAuditEntry(request, Audit.TargetType.Release, null, Audit.ActionType.Refresh);
    return YBPSuccess.empty();
  }

  @Deprecated
  @ApiOperation(
      value =
          "Deprecated: sinceVersion: 2024.1. Use ReleasesController.delete instead. Delete a"
              + " release",
      response = ReleaseManager.ReleaseMetadata.class,
      nickname = "deleteRelease")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, String version, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      Release release = Release.getByVersion(version);
      if (release == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Invalid Release version: " + version);
      }
      if (release.getUniverses().size() != 0) {
        throw new PlatformServiceException(BAD_REQUEST, "Release " + version + " is in use!");
      }
      if (!release.delete()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "failed to delete release: " + version);
      }
    } else {
      if (releaseManager.getReleaseByVersion(version) == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Invalid Release version: " + version);
      }

      if (releaseManager.getInUse(version)) {
        throw new PlatformServiceException(BAD_REQUEST, "Release " + version + " is in use!");
      }
      try {
        releaseManager.removeRelease(version);
      } catch (RuntimeException re) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, re.getMessage());
      }
    }
    auditService()
        .createAuditEntry(request, Audit.TargetType.Release, version, Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }
}
