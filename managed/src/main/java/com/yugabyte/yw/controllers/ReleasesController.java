package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.apiModels.CreateRelease;
import com.yugabyte.yw.controllers.apiModels.ResponseRelease;
import com.yugabyte.yw.controllers.apiModels.UpdateRelease;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPCreateSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.DB;
import io.ebean.Transaction;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.time.DateTimeException;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "New Release management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class ReleasesController extends AuthenticatedController {
  @Inject ReleasesUtils releasesUtils;

  @ApiOperation(
      value = "Create a release",
      response = YBPCreateSuccess.class,
      nickname = "createNewRelease",
      notes = "WARNING: This is a preview API that could change: create a ybdb release")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Release",
        value = "Release data to be created",
        required = true,
        dataType = "com.yugabyte.yw.controllers.apiModels.CreateRelease",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.3.0")
  public Result create(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    CreateRelease reqRelease =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), CreateRelease.class);

    // Validate the version
    releasesUtils.validateVersionAgainstCurrentYBA(reqRelease.version);

    // Validate the version/tag combo doesn't exist
    if (reqRelease.release_uuid == null) {
      log.trace("generating random release UUID as one was not provided");
      reqRelease.release_uuid = UUID.randomUUID();
    }
    Release release;
    try (Transaction transaction = DB.beginTransaction()) {
      release = Release.createFromRequest(reqRelease);
      if (reqRelease.artifacts != null) {
        for (CreateRelease.Artifact reqArtifact : reqRelease.artifacts) {
          ReleaseArtifact artifact = null;
          if (reqArtifact.package_file_id != null) {
            // Validate the local file actually exists.
            ReleaseLocalFile.getOrBadRequest(reqArtifact.package_file_id);
            artifact =
                ReleaseArtifact.create(
                    reqArtifact.sha256,
                    reqArtifact.platform,
                    reqArtifact.architecture,
                    reqArtifact.package_file_id);
          } else if (reqArtifact.package_url != null) {
            artifact =
                ReleaseArtifact.create(
                    reqArtifact.sha256,
                    reqArtifact.platform,
                    reqArtifact.architecture,
                    reqArtifact.package_url);
          } else {
            log.error(
                "invalid package, must specify exactly one of package file id or package url");
            throw new PlatformServiceException(
                BAD_REQUEST, "invalid artifact, no package url or package file id found");
          }
          release.addArtifact(artifact);
        }
      }
      transaction.commit();
    }
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Release, reqRelease.toString(), Audit.ActionType.Create);
    return new YBPCreateSuccess(release.getReleaseUUID()).asResult();
  }

  @ApiOperation(
      value = "List releases",
      response = ResponseRelease.class,
      responseContainer = "List",
      nickname = "listNewReleases",
      notes = "WARNING: This is a preview API that could change: list ybdb releases")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.3.0")
  public Result list(UUID customerUUID, @Nullable String deploymentType, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    List<Release> releases = null;
    if (deploymentType != null) {
      switch (deploymentType) {
        case "x86_64":
          releases =
              Release.getAllWithArtifactType(ReleaseArtifact.Platform.LINUX, Architecture.x86_64);
          break;
        case "aarch64":
          releases =
              Release.getAllWithArtifactType(ReleaseArtifact.Platform.LINUX, Architecture.aarch64);
          break;
        case "kubernetes":
          releases = Release.getAllWithArtifactType(ReleaseArtifact.Platform.KUBERNETES, null);
          break;
        default:
          log.error(
              "unknown deployment type {}, must be 'x86_64', 'aarch64', or 'kubernetes'",
              deploymentType);
          throw new PlatformServiceException(
              BAD_REQUEST, "unknown deployment type " + deploymentType);
      }
    } else {
      releases = Release.getAll();
    }
    Map<String, List<Universe>> versionUniverseMap = releasesUtils.versionUniversesMap();
    List<ResponseRelease> respReleases = new ArrayList<>();
    for (Release release : releases) {
      ResponseRelease resp = releaseToResponseRelease(release, versionUniverseMap);
      respReleases.add(resp);
    }
    return PlatformResults.withData(respReleases);
  }

  @ApiOperation(
      value = "Get a release",
      response = ResponseRelease.class,
      nickname = "getNewRelease",
      notes = "WARNING: This is a preview API that could change: get a specific ybdb release")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.3.0")
  public Result get(UUID customerUUID, UUID releaseUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Release release = Release.getOrBadRequest(releaseUUID);
    Map<String, List<Universe>> versionUniverseMap = releasesUtils.versionUniversesMap();
    ResponseRelease resp = releaseToResponseRelease(release, versionUniverseMap);
    return PlatformResults.withData(resp);
  }

  @ApiOperation(
      value = "delete a release",
      response = YBPSuccess.class,
      nickname = "deleteNewRelease",
      notes = "WARNING: This is a preview API that could change: delete a ybdb release")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.3.0")
  public Result delete(UUID customerUUID, UUID releaseUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Release release = Release.get(releaseUUID);
    if (release == null) {
      log.info("Release {} does not exist, skipping delete");
    } else {
      if (!release.getUniverses().isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "cannot delete in-use release " + releaseUUID);
      }
      if (!release.delete()) {
        log.error("Failed to delete release {}", releaseUUID);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "failed to delete release " + releaseUUID);
      }
    }
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Release, releaseUUID.toString(), Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Update a release",
      response = YBPSuccess.class,
      nickname = "updateNewRelease",
      notes = "WARNING: This is a preview API that could change: update or edit a ybdb release")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Release",
        value = "Release data to be updated",
        required = true,
        dataType = "com.yugabyte.yw.controllers.apiModels.UpdateRelease",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.3.0")
  public Result update(UUID customerUUID, UUID releaseUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Release release = Release.getOrBadRequest(releaseUUID);
    UpdateRelease reqRelease =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), UpdateRelease.class);

    // Update the release in a transaction
    try (Transaction transaction = DB.beginTransaction()) {

      // First, check for the easy fields (tag, date, and notes)
      if (reqRelease.release_tag != null
          && !reqRelease.release_tag.equals(release.getReleaseTag())) {
        log.debug("updating release tag to {}", reqRelease.release_tag);
        release.saveReleaseTag(reqRelease.release_tag);
      }
      if (reqRelease.release_date != null) {
        try {
          Date releaseDate = Date.from(Instant.ofEpochSecond(reqRelease.release_date));
          if (!releaseDate.equals(release.getReleaseDate())) {
            log.debug("updating release date to {}", reqRelease.release_date);
            release.saveReleaseDate(releaseDate);
          }
        } catch (IllegalArgumentException | DateTimeException e) {
          log.warn("unable to parse date format", e);
        }
      }
      if (reqRelease.release_notes != null
          && !reqRelease.release_notes.equals(release.getReleaseNotes())) {
        log.debug("updating release notes to {}", reqRelease.release_notes);
        release.saveReleaseNotes(reqRelease.release_notes);
      }
      if (reqRelease.state != null
          && !release.getState().equals(Release.ReleaseState.valueOf(reqRelease.state))) {
        if (!release.getUniverses().isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST, "cannot change state of in-use release " + releaseUUID);
        }
        log.info("updating release state to {}", reqRelease.state);
        release.saveState(Release.ReleaseState.valueOf(reqRelease.state));
      }

      // Now, check if any artifacts have changed, and update them as needed
      List<UUID> removeArtifacts =
          release.getArtifacts().stream()
              .map(a -> a.getArtifactUUID())
              .collect(Collectors.toList());
      if (reqRelease.artifacts != null) {
        for (UpdateRelease.Artifact reqArtifact : reqRelease.artifacts) {
          boolean found = false;
          for (ReleaseArtifact artifact : release.getArtifacts()) {
            if ((reqArtifact.package_file_id != null
                    && reqArtifact.package_file_id.equals(artifact.getPackageFileID()))
                || (reqArtifact.package_url != null
                    && reqArtifact.package_url.equals(artifact.getPackageURL()))) {
              found = true;
              removeArtifacts.remove(artifact.getArtifactUUID());
              if (reqArtifact.platform != null && reqArtifact.platform != artifact.getPlatform()) {
                log.error(
                    String.format(
                        "cannot update artifact platform for %s", artifact.getArtifactUUID()));
                throw new PlatformServiceException(
                    BAD_REQUEST, "cannot update an artifacts platform");
              }
              if (reqArtifact.architecture != null
                  && reqArtifact.architecture != artifact.getArchitecture()) {
                log.error(
                    String.format(
                        "cannot update artifact architecture for %s", artifact.getArtifactUUID()));
                throw new PlatformServiceException(
                    BAD_REQUEST, "cannot update an artifacts architecture");
              }
              if (reqArtifact.sha256 != null && !reqArtifact.sha256.equals(artifact.getSha256())) {
                log.info(
                    "updating artifact {} sha to {}",
                    artifact.getArtifactUUID(),
                    reqArtifact.sha256);
                artifact.saveSha256(reqArtifact.sha256);
              }
              break;
            }
          }
          if (!found) {
            log.info("creating new artifact");
            if (reqArtifact.package_file_id != null) {
              ReleaseArtifact newArtifact =
                  ReleaseArtifact.create(
                      reqArtifact.sha256,
                      reqArtifact.platform,
                      reqArtifact.architecture,
                      reqArtifact.package_file_id);
              release.addArtifact(newArtifact);
            }
            if (reqArtifact.package_url != null) {
              ReleaseArtifact newArtifact =
                  ReleaseArtifact.create(
                      reqArtifact.sha256,
                      reqArtifact.platform,
                      reqArtifact.architecture,
                      reqArtifact.package_url);
              release.addArtifact(newArtifact);
            }
          }
        }
      }
      for (UUID artifactUUID : removeArtifacts) {
        if (!release.getUniverses().isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST, "cannot remove artifacts from in-use release " + releaseUUID);
        }
        ReleaseArtifact artifact = ReleaseArtifact.get(artifactUUID);
        if (artifact != null) {
          log.info("deleting artifact {}", artifact.getArtifactUUID());
          artifact.delete();
        } else {
          log.debug("Skipping delete of artifact {} - doesn't exist", artifactUUID);
        }
      }
      // And finally commit the transaction
      transaction.commit();
    }
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Release, reqRelease.toString(), Audit.ActionType.Update);
    return YBPSuccess.empty();
  }

  private ResponseRelease releaseToResponseRelease(
      Release release, Map<String, List<Universe>> versionUniversesMap) {
    ResponseRelease resp = new ResponseRelease();
    resp.release_uuid = release.getReleaseUUID();
    resp.version = release.getVersion();
    resp.yb_type = release.getYb_type().toString();
    resp.release_type = release.getReleaseType();
    resp.state = release.getState().toString();
    if (release.getReleaseDate() != null) {
      resp.release_date_msecs = release.getReleaseDate().toInstant().toEpochMilli();
    }
    resp.release_notes = release.getReleaseNotes();
    resp.release_tag = release.getReleaseTag();
    resp.artifacts = new ArrayList<>();
    for (ReleaseArtifact artifact : release.getArtifacts()) {
      ResponseRelease.Artifact respArtifact = new ResponseRelease.Artifact();
      respArtifact.platform = artifact.getPlatform();
      respArtifact.architecture = artifact.getArchitecture();
      if (artifact.getPackageFileID() != null) {
        respArtifact.package_file_id = artifact.getPackageFileID();
        ReleaseLocalFile rlf = ReleaseLocalFile.get(artifact.getPackageFileID());
        respArtifact.file_name = rlf.getLocalFilePath();
      } else {
        respArtifact.package_url = artifact.getPackageURL();
      }
      resp.artifacts.add(respArtifact);
    }

    resp.universes =
        versionUniversesMap.getOrDefault(release.getVersion(), new ArrayList<Universe>()).stream()
            .map(
                u ->
                    new ResponseRelease.Universe(
                        u.getUniverseUUID(), u.getName(), u.getCreationDate()))
            .collect(Collectors.toList());
    return resp;
  }
}
