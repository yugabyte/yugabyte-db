// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import static play.mvc.Results.ok;

import api.v2.handlers.SupportBundleManagementHandler;
import api.v2.models.SupportBundle;
import api.v2.models.SupportBundleComponentType;
import api.v2.models.SupportBundleCreateSpec;
import api.v2.models.SupportBundlePagedQuerySpec;
import api.v2.models.SupportBundlePagedResp;
import api.v2.models.SupportBundleSizeEstimateResponse;
import api.v2.models.YBATask;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import java.io.InputStream;
import java.util.List;
import java.util.UUID;
import play.mvc.Http.Request;
import play.mvc.Result;

public class SupportBundleApiControllerImp extends SupportBundleApiControllerImpInterface {

  private final SupportBundleManagementHandler handler;

  @Inject
  public SupportBundleApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      SupportBundleManagementHandler handler) {
    super(auditService, config, gFlagsAuditHandler);
    this.handler = handler;
  }

  @Override
  public YBATask createSupportBundle(
      Request request, UUID cUUID, UUID uniUUID, SupportBundleCreateSpec supportBundleCreateSpec) {
    return handler.createSupportBundle(cUUID, uniUUID, supportBundleCreateSpec);
  }

  @Override
  public YBATask createYbaSupportBundle(
      Request request, UUID cUUID, SupportBundleCreateSpec supportBundleCreateSpec) {
    return handler.createYbaSupportBundle(cUUID, supportBundleCreateSpec);
  }

  @Override
  public void deleteSupportBundle(Request request, UUID cUUID, UUID uniUUID, UUID sbUUID) {
    handler.deleteSupportBundle(cUUID, uniUUID, sbUUID);
  }

  @Override
  public void deleteYbaSupportBundle(Request request, UUID cUUID, UUID sbUUID) {
    handler.deleteYbaSupportBundle(cUUID, sbUUID);
  }

  @Override
  public InputStream downloadSupportBundle(Request request, UUID cUUID, UUID uniUUID, UUID sbUUID) {
    return handler.downloadSupportBundle(cUUID, uniUUID, sbUUID);
  }

  @Override
  public Result downloadSupportBundleHttp(Request request, UUID cUUID, UUID uniUUID, UUID sbUUID)
      throws Exception {
    InputStream is = handler.downloadSupportBundle(cUUID, uniUUID, sbUUID);
    String filename = handler.getDownloadFileName(cUUID, uniUUID, sbUUID);
    return ok(is)
        .as("application/x-compressed")
        .withHeader("Content-Disposition", "attachment; filename=" + filename);
  }

  @Override
  public InputStream downloadYbaSupportBundle(Request request, UUID cUUID, UUID sbUUID) {
    return handler.downloadYbaSupportBundle(cUUID, sbUUID);
  }

  @Override
  public Result downloadYbaSupportBundleHttp(Request request, UUID cUUID, UUID sbUUID)
      throws Exception {
    InputStream is = handler.downloadYbaSupportBundle(cUUID, sbUUID);
    String filename = handler.getYbaDownloadFileName(cUUID, sbUUID);
    return ok(is)
        .as("application/x-compressed")
        .withHeader("Content-Disposition", "attachment; filename=" + filename);
  }

  @Override
  public SupportBundleSizeEstimateResponse estimateSupportBundleSize(
      Request request, UUID cUUID, UUID uniUUID, SupportBundleCreateSpec supportBundleCreateSpec)
      throws Exception {
    return handler.estimateSupportBundleSize(cUUID, uniUUID, supportBundleCreateSpec);
  }

  @Override
  public SupportBundleSizeEstimateResponse estimateYbaSupportBundleSize(
      Request request, UUID cUUID, SupportBundleCreateSpec supportBundleCreateSpec)
      throws Exception {
    return handler.estimateYbaSupportBundleSize(cUUID, supportBundleCreateSpec);
  }

  @Override
  public SupportBundle getSupportBundle(Request request, UUID cUUID, UUID uniUUID, UUID sbUUID) {
    return handler.getSupportBundle(cUUID, uniUUID, sbUUID);
  }

  @Override
  public SupportBundle getYbaSupportBundle(Request request, UUID cUUID, UUID sbUUID) {
    return handler.getYbaSupportBundle(cUUID, sbUUID);
  }

  @Override
  public List<SupportBundleComponentType> listSupportBundleComponents(Request request, UUID cUUID) {
    return handler.listSupportBundleComponents(cUUID);
  }

  @Override
  public SupportBundlePagedResp pageListSupportBundles(
      Request request,
      UUID cUUID,
      UUID uniUUID,
      SupportBundlePagedQuerySpec supportBundlePagedQuerySpec) {
    return handler.pageListSupportBundles(cUUID, uniUUID, supportBundlePagedQuerySpec);
  }

  @Override
  public SupportBundlePagedResp pageListYbaSupportBundles(
      Request request, UUID cUUID, SupportBundlePagedQuerySpec supportBundlePagedQuerySpec) {
    return handler.pageListYbaSupportBundles(cUUID, supportBundlePagedQuerySpec);
  }
}
