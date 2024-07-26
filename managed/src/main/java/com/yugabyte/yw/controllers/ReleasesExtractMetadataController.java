package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.apiModels.ExtractMetadata;
import com.yugabyte.yw.controllers.apiModels.ResponseExtractMetadata;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPCreateSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Customer;
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
import java.net.MalformedURLException;
import java.net.URL;
import java.util.Collections;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.map.LRUMap;
import play.libs.ws.WSClient;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Extract metadata from remote tarball",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class ReleasesExtractMetadataController extends AuthenticatedController {

  @Inject WSClient wsClient;
  @Inject ReleasesUtils releasesUtils;

  public final int MaxPoolSize = 5;
  private ThreadPoolExecutor threadPool =
      (ThreadPoolExecutor) Executors.newFixedThreadPool(MaxPoolSize);

  private Map<UUID, ResponseExtractMetadata> metadataMap =
      Collections.synchronizedMap(new LRUMap<UUID, ResponseExtractMetadata>(100));

  @ApiOperation(
      value = "helper to extract release metadata from a remote tarball",
      response = YBPSuccess.class,
      nickname = "extractMetadata",
      notes =
          "WARNING: This is a preview API that could change: start extracting metadata from a"
              + " remote tgz url")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Release URL",
        value = "Release URL to extract metadata from",
        required = true,
        dataType = "com.yugabyte.yw.controllers.apiModels.ExtractMetadata",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.2.0.0")
  public Result extract_metadata(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    ExtractMetadata em =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), ExtractMetadata.class);

    // generate a uuid if one is needed
    if (em.uuid == null) {
      em.uuid = UUID.randomUUID();
    }
    if (metadataMap.containsKey(em.uuid)) {
      log.error("found metadata with uuid " + em.uuid);
      throw new PlatformServiceException(BAD_REQUEST, "metadata already exists");
    }
    // Basic URL Validation.
    if (em.url == null || em.url.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "must provide a url");
    }
    URL url;
    try {
      url = new URL(em.url);
    } catch (MalformedURLException e) {
      log.error("Invalid url " + em.url, e);
      throw new PlatformServiceException(BAD_REQUEST, "invalid url " + em.url);
    }
    ResponseExtractMetadata metadata = new ResponseExtractMetadata();
    metadata.metadata_uuid = em.uuid;
    metadata.status = ResponseExtractMetadata.Status.waiting;
    metadataMap.put(em.uuid, metadata);
    log.debug("starting thread for extract metadata " + em.uuid);
    Thread runner =
        new Thread() {
          public void run() {
            downloadAndExtract(em.uuid, url);
          }
        };
    threadPool.submit(runner);
    return new YBPCreateSuccess(em.uuid).asResult();
  }

  @ApiOperation(
      value = "get the extract release metadata from a remote tarball",
      response = ResponseExtractMetadata.class,
      nickname = "extractMetadata",
      notes =
          "WARNING: This is a preview API that could change: Get extract metadata and its"
              + " progress.")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.2.0.0")
  public Result getMetadata(UUID customerUUID, UUID metadataUUID, Http.Request request) {
    ResponseExtractMetadata metadata = metadataMap.get(metadataUUID);
    Customer.getOrBadRequest(customerUUID);
    if (metadata == null) {
      throw new PlatformServiceException(NOT_FOUND, "no metadata found with uuid " + metadataUUID);
    }
    return PlatformResults.withData(metadata);
  }

  private void downloadAndExtract(UUID metadataUuid, URL url) {
    ResponseExtractMetadata metadata = metadataMap.get(metadataUuid);
    metadata.status = ResponseExtractMetadata.Status.running;
    log.info("reading file from {}", url.toString());
    try {
      ReleasesUtils.ExtractedMetadata em = releasesUtils.versionMetadataFromURL(url);
      metadata.populateFromExtractedMetadata(em);
      metadata.status = ResponseExtractMetadata.Status.success;
    } catch (Exception e) {
      metadata.status = ResponseExtractMetadata.Status.failure;
      log.error("failed to extract metadata", e);
    }
  }
}
