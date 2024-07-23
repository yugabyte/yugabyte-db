package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.controllers.apiModels.ResponseExtractMetadata;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPCreateSuccess;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ReleaseLocalFile;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FilenameUtils;
import play.libs.Files.TemporaryFile;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Upload Release packages",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH),
    hidden = true)
@Slf4j
public class ReleasesUploadController extends AuthenticatedController {

  private final Config appConfig;
  private final ReleasesUtils releaseUtils;

  @Inject
  public ReleasesUploadController(Config appConfig, ReleasesUtils releasesUtils) {
    this.appConfig = appConfig;
    this.releaseUtils = releasesUtils;
  }

  @ApiOperation(
      value = "upload a release tgz",
      nickname = "uploadRelease",
      notes = "YbaApi Internal upload release",
      hidden = true) // TODO: remove hidden once complete.
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.21.1.0")
  public Result upload(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    // Later, we can implement resumable uploads and take in a UUID as an option.
    UUID uuid = UUID.randomUUID();
    Http.MultipartFormData<TemporaryFile> body = request.body().asMultipartFormData();
    Http.MultipartFormData.FilePart<TemporaryFile> filePart = body.getFile("file");
    if (filePart == null) {
      throw new PlatformServiceException(BAD_REQUEST, "no 'file' found in body");
    }
    String fileName = FilenameUtils.getName(filePart.getFilename());
    Path destPath = Paths.get(releaseUtils.getUploadStoragePath(), uuid.toString(), fileName);
    if (!destPath.getParent().toFile().mkdir()) {
      log.error("failed to create directory {}", destPath.getParent());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "failed to create directory " + destPath.getParent());
    }
    TemporaryFile targetFile = filePart.getRef();
    try {
      FileUtils.moveFile(targetFile.path(), destPath);
    } catch (IOException e) {
      log.error("failed to download file", e);
      if (!destPath.toFile().delete()) {
        log.error("failed to delete {}", destPath);
      } else if (!destPath.getParent().toFile().delete()) {
        log.error("failed to delete directory {}", destPath.getParent());
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "failed to upload file");
    }
    ReleaseLocalFile.create(uuid, destPath.toString(), true);
    log.info("downloaded file to {}", destPath);
    return new YBPCreateSuccess(uuid).asResult();
  }

  @ApiOperation(
      value = "get an uploaded release metadata",
      nickname = "getUploadRelease",
      notes = "YbaApi Internal get uploaded release metadata",
      response = ResponseExtractMetadata.class,
      hidden = true) // TODO: remove hidden once complete.
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.21.1.0")
  public Result get(UUID customerUUID, UUID fileUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    ReleaseLocalFile rlf = ReleaseLocalFile.getOrBadRequest(fileUUID);
    // TODO: This is a bit slow, we should find a way to have this calculated and stored as part
    // of the upload: PLAT-12687
    ReleasesUtils.ExtractedMetadata em =
        releaseUtils.metadataFromPath(Paths.get(rlf.getLocalFilePath()));
    ResponseExtractMetadata metadata = ResponseExtractMetadata.fromExtractedMetadata(em);
    metadata.status = ResponseExtractMetadata.Status.success;
    return PlatformResults.withData(metadata);
  }
}
