package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.apiModels.CreateRelease;
import com.yugabyte.yw.controllers.apiModels.ResponseRelease;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPCreateSuccess;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
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
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "New Release management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH),
    hidden = true)
@Slf4j
public class ReleasesController extends AuthenticatedController {

  @ApiOperation(
      value = "Create a release",
      response = YBPCreateSuccess.class,
      nickname = "createNewRelease",
      notes = "YbaApi Internal new releases list",
      hidden = true) // TODO: remove hidden once complete.
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
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.21.1.0")
  public Result create(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    CreateRelease reqRelease =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), CreateRelease.class);
    if (reqRelease.release_uuid == null) {
      log.debug("generating random release UUID as one was not provided");
      reqRelease.release_uuid = UUID.randomUUID();
    }
    Release release = Release.createFromRequest(reqRelease);
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
        } else {
          artifact =
              ReleaseArtifact.create(
                  reqArtifact.sha256,
                  reqArtifact.platform,
                  reqArtifact.architecture,
                  reqArtifact.package_url);
        }
        release.addArtifact(artifact);
      }
    }
    return new YBPCreateSuccess(release.getReleaseUUID()).asResult();
  }

  @ApiOperation(
      value = "List releases",
      response = ResponseRelease.class,
      responseContainer = "List",
      nickname = "listNewReleases",
      notes = "YbaApi Internal new releases list",
      hidden = true) // TODO: Remove hidden once complete
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.21.1.0")
  public Result list(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    List<Release> releases = Release.getAll();
    List<ResponseRelease> respReleases = new ArrayList<>();
    for (Release release : releases) {
      ResponseRelease resp = new ResponseRelease();
      resp.releaseUuid = release.getReleaseUUID();
      resp.version = release.getVersion();
      resp.yb_type = release.getYb_type().toString();
      resp.releaseType = release.getReleaseType();
      if (release.getReleaseDate() != null) {
        resp.releaseDate = release.getReleaseDate().toString();
      }
      resp.releaseNotes = release.getReleaseNotes();
      resp.releaseTag = release.getReleaseTag();
      resp.artifacts = new ArrayList<>();
      for (ReleaseArtifact artifact : release.getArtifacts()) {
        ResponseRelease.Artifact respArtifact = new ResponseRelease.Artifact();
        if (artifact.getPackageFileID() != null) {
          respArtifact.packageFileID = artifact.getPackageFileID();
          ReleaseLocalFile rlf = ReleaseLocalFile.get(artifact.getPackageFileID());
          respArtifact.fileName = new File(rlf.getLocalFilePath()).getName();
        } else {
          respArtifact.packageURL = artifact.getPackageURL();
        }
        resp.artifacts.add(respArtifact);
      }
      respReleases.add(resp);
    }
    return PlatformResults.withData(respReleases);
  }
}
