package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.apiModels.ExtractMetadata;
import com.yugabyte.yw.controllers.apiModels.ResponseExtractMetadata;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPCreateSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
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
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.DateFormat;
import java.text.ParseException;
import java.util.Collections;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.map.LRUMap;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.io.FileUtils;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Extract metadata from remote tarball",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH),
    hidden = true)
@Slf4j
public class ReleasesExtractMetadataController extends AuthenticatedController {

  @Inject WSClient wsClient;

  public final int MaxPoolSize = 5;
  private ThreadPoolExecutor threadPool =
      (ThreadPoolExecutor) Executors.newFixedThreadPool(MaxPoolSize);

  private Map<UUID, ResponseExtractMetadata> metadataMap =
      Collections.synchronizedMap(new LRUMap<UUID, ResponseExtractMetadata>(100));

  @ApiOperation(
      value = "helper to extract release metadata from a remote tarball",
      response = YBPSuccess.class,
      nickname = "extractMetadata",
      notes = "YbaApi Internal extract metadata",
      hidden = true)
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
      notes = "YbaApi Internal extract metadata",
      hidden = true)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
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
    log.info("download file from {}", url.toString());
    File file = null;
    try {
      file = File.createTempFile("temp", ".tgz");
      file.deleteOnExit(); // set delete on exit in case we crash before we can delete the file.
      log.debug("downloading to temp file {}", file.getAbsolutePath());
      FileUtils.copyURLToFile(url, file);
      log.info("calculating file {} sha256", file.getAbsolutePath());
      String sha256 = null;
      try {
        Path fp = Paths.get(file.getAbsolutePath());
        sha256 = Util.computeFileChecksum(fp, "SHA256");
        log.debug("calculated sha {}", sha256);
      } catch (Exception e) {
        log.warn("failed to compute file checksum", e);
      }
      log.info("extracting metadata from {}", file.getAbsolutePath());
      try (InputStream fIn = new BufferedInputStream(new FileInputStream(file));
          GzipCompressorInputStream gzIn = new GzipCompressorInputStream(fIn);
          TarArchiveInputStream tarInput = new TarArchiveInputStream(gzIn)) {
        TarArchiveEntry entry;
        while ((entry = tarInput.getNextEntry()) != null) {
          if (entry.getName().endsWith("version_metadata.json")) {
            log.debug("found version_metadata.json");
            // We can reasonably assume that the version metadata json is small enough to read in
            // oneshot
            byte[] fileContent = new byte[(int) entry.getSize()];
            tarInput.read(fileContent, 0, fileContent.length);
            log.debug("read version_metadata.json string: {}", new String(fileContent));
            JsonNode node = Json.parse(fileContent);
            metadata.yb_type = Release.YbType.YBDB;
            metadata.sha256 = sha256;

            // Populate required fields from version metadata. Bad Request if required fields do not
            // exist.
            if (node.has("version_number") && node.has("build_number")) {
              metadata.version =
                  String.format(
                      "%s-b%s",
                      node.get("version_number").asText(), node.get("build_number").asText());
            } else {
              log.error("no 'version_number' and 'build_number' found in metadata");
              metadata.status = ResponseExtractMetadata.Status.failure;
              return;
            }
            if (node.has("platform")) {
              metadata.platform =
                  ReleaseArtifact.Platform.valueOf(node.get("platform").asText().toUpperCase());
            } else {
              log.error("no 'platform' found in metadata");
              metadata.status = ResponseExtractMetadata.Status.failure;
              return;
            }
            // TODO: release type should be mandatory
            if (node.has("release_type")) {
              metadata.release_type = node.get("release_type").asText();
            } else {
              log.warn("no release type, default to PREVIEW");
              metadata.release_type = "PREVIEW (DEFAULT)";
            }
            // Only Linux platform has architecture. K8S expects null value for architecture.
            if (metadata.platform.equals(ReleaseArtifact.Platform.LINUX)) {
              if (node.has("architecture")) {
                metadata.architecture = Architecture.valueOf(node.get("architecture").asText());
              } else {
                log.error("no 'architecture' for linux platform");
                metadata.status = ResponseExtractMetadata.Status.failure;
              }
            }

            // Populate optional sections if available.
            if (node.has("release_date")) {
              DateFormat df = DateFormat.getDateInstance();
              try {
                metadata.release_date = df.parse(node.get("release_date").asText());
                // best effort parse.
              } catch (ParseException e) {
                log.warn("invalid date format", e);
              }
            }
            if (node.has("release_notes")) {
              metadata.release_notes = node.get("release_notes").asText();
            }
            log.info("finished extracting metadata " + metadataUuid);
            metadata.status = ResponseExtractMetadata.Status.success;
            return;
          }
        }
        // While loop completed, mark as failure since no version metadata was found.
        metadata.status = ResponseExtractMetadata.Status.failure;
        log.error("no version_metadata.json found");
      } catch (IOException e) {
        log.error("failed extracting metadata", e);
        metadata.status = ResponseExtractMetadata.Status.failure;
      }
    } catch (Exception e) {
      metadata.status = ResponseExtractMetadata.Status.failure;
      log.error("failed to extract metadata", e);
    } finally {
      // Delete the temporary file
      if (file != null && !file.delete()) {
        log.error("failed to delete file at {}" + file.getAbsolutePath());
      }
    }
  }
}
