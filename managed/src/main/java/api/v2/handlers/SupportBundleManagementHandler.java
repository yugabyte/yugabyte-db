// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.SupportBundleComponentSpecMapper;
import api.v2.mappers.SupportBundleCreateSpecMapper;
import api.v2.mappers.SupportBundleEnumMapper;
import api.v2.mappers.SupportBundleMapper;
import api.v2.mappers.SupportBundleSizeEstimateMapper;
import api.v2.models.SupportBundle;
import api.v2.models.SupportBundleComponentType;
import api.v2.models.SupportBundleCreateSpec;
import api.v2.models.SupportBundlePagedQuerySpec;
import api.v2.models.SupportBundlePagedResp;
import api.v2.models.SupportBundleSizeEstimateResponse;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import api.v2.utils.NormalizedPaginationSpec;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleScriptParamResolver;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestMerger;
import com.yugabyte.yw.controllers.handlers.SupportBundleHandlerV2;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.YCQLComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.YSQLComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.YbAdminComponentSpec;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.InputStream;
import java.util.EnumSet;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class SupportBundleManagementHandler extends ApiControllerUtils {

  private final Commissioner commissioner;
  private final SupportBundleUtil supportBundleUtil;
  private final SupportBundleHandlerV2 sbHandler;
  private final RuntimeConfGetter confGetter;
  private final SupportBundleV2ManifestMerger manifestMerger;

  @Inject
  public SupportBundleManagementHandler(
      AuditService auditService,
      Commissioner commissioner,
      SupportBundleUtil supportBundleUtil,
      SupportBundleHandlerV2 sbHandler,
      RuntimeConfGetter confGetter,
      SupportBundleV2ManifestMerger manifestMerger) {
    super(auditService);
    this.commissioner = commissioner;
    this.supportBundleUtil = supportBundleUtil;
    this.sbHandler = sbHandler;
    this.confGetter = confGetter;
    this.manifestMerger = manifestMerger;
  }

  public YBATask createSupportBundle(
      UUID customerUUID, UUID universeUUID, SupportBundleCreateSpec createSpec) {
    Customer customer = Customer.getOrNotFound(customerUUID);
    Universe universe = Universe.getOrNotFound(universeUUID, customerUUID);

    if (universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().universePaused) {
      log.info(
          "Trying to create support bundle while universe {} is "
              + "in a locked/paused state or has backup running.",
          universe.getName());
    }

    // The YBA-only entry points share toFormData but are left out on purpose: their validation
    // requires components to hold YBAComponent alone.
    SupportBundleCreateSpec request = manifestMerger.mergeIfNeeded(createSpec);
    SupportBundleFormDataV2 bundleData = toFormData(request);
    sbHandler.bundleDataValidation(bundleData, universe);
    useManifestOwnedValues(bundleData, request);

    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, confGetter);
    SupportBundleTaskParamsV2 taskParams =
        new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe);
    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundleV2, taskParams);

    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreateSupportBundleV2,
        universe.getName());
    log.info(
        "Saved task uuid {} in customer tasks table for customer: {} and universe: {}",
        taskUUID,
        customerUUID,
        universeUUID);

    return new YBATask().taskUuid(taskUUID).resourceUuid(supportBundle.getBundleUUID());
  }

  public YBATask createYbaSupportBundle(UUID customerUUID, SupportBundleCreateSpec createSpec) {
    Customer customer = Customer.getOrNotFound(customerUUID);

    SupportBundleFormDataV2 bundleData = toFormData(createSpec);
    sbHandler.bundleDataValidationYbaOnly(bundleData);
    useManifestOwnedValues(bundleData, createSpec);

    SupportBundleV2 supportBundle = SupportBundleV2.createYbaOnly(bundleData, customer, confGetter);
    SupportBundleTaskParamsV2 taskParams =
        SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);
    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundleV2, taskParams);

    CustomerTask.create(
        customer,
        customerUUID,
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.CreateSupportBundleV2,
        "YBA platform");
    log.info(
        "Saved task uuid {} in customer tasks table for YBA-only support bundle, customer: {}",
        taskUUID,
        customerUUID);

    return new YBATask().taskUuid(taskUUID).resourceUuid(supportBundle.getBundleUUID());
  }

  public SupportBundlePagedResp pageListSupportBundles(
      UUID customerUUID, UUID universeUUID, SupportBundlePagedQuerySpec spec) {
    Universe.getOrNotFound(universeUUID, customerUUID);
    NormalizedPaginationSpec normalized = HandlerPagingSupport.normalize(spec);
    return HandlerPagingSupport.pagedResponse(
        new SupportBundlePagedResp(),
        SupportBundleV2.getPagedList(universeUUID, normalized),
        toApi(retentionDays()));
  }

  public SupportBundlePagedResp pageListYbaSupportBundles(
      UUID customerUUID, SupportBundlePagedQuerySpec spec) {
    Customer.getOrNotFound(customerUUID);
    NormalizedPaginationSpec normalized = HandlerPagingSupport.normalize(spec);
    return HandlerPagingSupport.pagedResponse(
        new SupportBundlePagedResp(),
        SupportBundleV2.getPagedListForCustomer(customerUUID, normalized),
        toApi(retentionDays()));
  }

  public SupportBundle getSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    SupportBundleV2 bundle = universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID);
    return SupportBundleMapper.INSTANCE.toApi(bundle, retentionDays());
  }

  public SupportBundle getYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    SupportBundleV2 bundle = ybaBundleOrNotFound(customerUUID, bundleUUID);
    return SupportBundleMapper.INSTANCE.toApi(bundle, retentionDays());
  }

  public void deleteSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    SupportBundleV2 supportBundle =
        universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID);

    if (SupportBundleV2StatusType.Running.equals(supportBundle.getStatus())) {
      throw new PlatformServiceException(CONFLICT, "The support bundle is in running state.");
    }

    supportBundleUtil.deleteSupportBundleV2(supportBundle);

    log.info("Successfully deleted the support bundle: {}", bundleUUID);
  }

  public void deleteYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    SupportBundleV2 supportBundle = ybaBundleOrNotFound(customerUUID, bundleUUID);

    if (SupportBundleV2StatusType.Running.equals(supportBundle.getStatus())) {
      throw new PlatformServiceException(CONFLICT, "The support bundle is in running state.");
    }

    supportBundleUtil.deleteSupportBundleV2(supportBundle);

    log.info("Successfully deleted the YBA-only support bundle: {}", bundleUUID);
  }

  public InputStream downloadSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    return toInputStream(universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID));
  }

  public InputStream downloadYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    return toInputStream(ybaBundleOrNotFound(customerUUID, bundleUUID));
  }

  public String getDownloadFileName(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    return universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID).getFileName();
  }

  public String getYbaDownloadFileName(UUID customerUUID, UUID bundleUUID) {
    return ybaBundleOrNotFound(customerUUID, bundleUUID).getFileName();
  }

  public List<SupportBundleComponentType> listSupportBundleComponents(UUID customerUUID) {
    Customer.getOrNotFound(customerUUID);
    return EnumSet.allOf(ComponentType.class).stream()
        .map(SupportBundleEnumMapper.INSTANCE::toComponentType)
        .collect(Collectors.toList());
  }

  public SupportBundleSizeEstimateResponse estimateSupportBundleSize(
      UUID customerUUID, UUID universeUUID, SupportBundleCreateSpec createSpec) throws Exception {
    Customer customer = Customer.getOrNotFound(customerUUID);
    Universe universe = Universe.getOrNotFound(universeUUID, customerUUID);

    // Same merge as createSupportBundle, so the estimate sizes what a create would actually
    // collect. The size estimator dispatches on the v2 component buckets, which only exist once
    // the manifest has filled in the spec lists.
    SupportBundleCreateSpec request = manifestMerger.mergeIfNeeded(createSpec);
    SupportBundleFormDataV2 bundleData = toFormData(request);
    sbHandler.bundleDataValidation(bundleData, universe);
    useManifestOwnedValues(bundleData, request);

    return SupportBundleSizeEstimateMapper.INSTANCE.toApi(
        sbHandler.estimateBundleSize(customer, bundleData, universe));
  }

  public SupportBundleSizeEstimateResponse estimateYbaSupportBundleSize(
      UUID customerUUID, SupportBundleCreateSpec createSpec) throws Exception {
    Customer customer = Customer.getOrNotFound(customerUUID);

    SupportBundleFormDataV2 bundleData = toFormData(createSpec);
    sbHandler.bundleDataValidationYbaOnly(bundleData);
    useManifestOwnedValues(bundleData, createSpec);

    return SupportBundleSizeEstimateMapper.INSTANCE.toApi(
        sbHandler.estimateBundleSizeYbaOnly(customer, bundleData));
  }

  /**
   * Authorization only covers the universe named in the path, so every lookup has to tie the bundle
   * back to it. Going through here rather than {@link SupportBundleV2#getOrNotFound} keeps that
   * from being forgotten.
   */
  private SupportBundleV2 universeBundleOrNotFound(
      UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    Universe.getOrNotFound(universeUUID, customerUUID);
    SupportBundleV2 bundle = SupportBundleV2.getOrNotFound(bundleUUID);
    bundle.verifyUniverseScope(universeUUID);
    return bundle;
  }

  private SupportBundleV2 ybaBundleOrNotFound(UUID customerUUID, UUID bundleUUID) {
    Customer.getOrNotFound(customerUUID);
    SupportBundleV2 bundle = SupportBundleV2.getOrNotFound(bundleUUID);
    bundle.verifyCustomerScope(customerUUID);
    return bundle;
  }

  /** Runs after validation, so a spec that does not match the manifest is still a 400. */
  private void useManifestOwnedValues(
      SupportBundleFormDataV2 bundleData, SupportBundleCreateSpec request) {
    useManifestParams(bundleData, request);
    useManifestQueriesAndArgs(bundleData);
    useManifestTimeouts(bundleData);
    useManifestTarPaths(bundleData.filesComponentSpecs);
    useManifestTarPaths(bundleData.bashComponentSpecs);
    useManifestTarPaths(bundleData.ybaComponentSpecs);
  }

  /** The script writes its tar, and YBA copies and deletes it, only at the manifest's path. */
  private void useManifestTarPaths(List<? extends ScriptTarComponentSpec> specs) {
    if (specs == null) {
      return;
    }
    for (ScriptTarComponentSpec spec : specs) {
      String tarPath = manifestMerger.remoteTarPath(spec.getComponentName());
      List<String> params = SupportBundleScriptParamResolver.withTarPath(spec.getParams(), tarPath);
      if (!tarPath.equals(spec.getRemoteTarPath()) || !params.equals(spec.getParams())) {
        log.warn(
            "Component '{}' writes its tar to '{}' from YBA's manifest, not where the request said",
            spec.getComponentName(),
            tarPath);
      }
      spec.setRemoteTarPath(tarPath);
      spec.setParams(params);
    }
  }

  /**
   * Files and Bash members run with the arguments in YBA's manifest, never the request's: those
   * arguments choose which files a component reads. The request's own fields (filter_pg_audit_logs,
   * the core limits) are applied to the manifest's spec, as the fill applies them.
   */
  private void useManifestParams(
      SupportBundleFormDataV2 bundleData, SupportBundleCreateSpec request) {
    useManifestParams(
        bundleData.filesComponentSpecs,
        name ->
            SupportBundleComponentSpecMapper.INSTANCE
                .toV1(
                    manifestMerger.buildFilesComponentSpec(
                        name, bundleData.startDate, bundleData.endDate, request))
                .getParams());
    useManifestParams(
        bundleData.bashComponentSpecs,
        name ->
            SupportBundleComponentSpecMapper.INSTANCE
                .toV1(
                    manifestMerger.buildBashComponentSpec(
                        name, bundleData.startDate, bundleData.endDate))
                .getParams());
  }

  private void useManifestParams(
      List<? extends ScriptTarComponentSpec> specs, Function<String, List<String>> manifestParams) {
    if (specs == null) {
      return;
    }
    for (ScriptTarComponentSpec spec : specs) {
      List<String> params = manifestParams.apply(spec.getComponentName());
      if (!Objects.equals(spec.getParams(), params)) {
        log.warn(
            "Component '{}' runs with the arguments in YBA's manifest {} rather than the"
                + " request's {}",
            spec.getComponentName(),
            params,
            spec.getParams());
      }
      spec.setParams(params);
    }
  }

  /**
   * YSQL and YCQL members run the queries in YBA's manifest, and yb-admin members its arguments,
   * never the request's: ysqlsh connects as a superuser, so a query filter can't vouch for
   * arbitrary SQL, and yb-admin reads a flag such as --master_addresses anywhere on its command
   * line. A new member needs only a manifest entry.
   */
  private void useManifestQueriesAndArgs(SupportBundleFormDataV2 bundleData) {
    if (bundleData.ysqlComponentSpecs != null) {
      for (YSQLComponentSpec spec : bundleData.ysqlComponentSpecs) {
        spec.setQueries(
            manifestOwned(
                "YSQL",
                "queries",
                spec.getComponentName(),
                spec.getQueries(),
                manifestMerger.ysqlQueries(spec.getComponentName())));
      }
    }
    if (bundleData.ycqlComponentSpecs != null) {
      for (YCQLComponentSpec spec : bundleData.ycqlComponentSpecs) {
        spec.setQueries(
            manifestOwned(
                "YCQL",
                "queries",
                spec.getComponentName(),
                spec.getQueries(),
                manifestMerger.ycqlQueries(spec.getComponentName())));
      }
    }
    if (bundleData.ybAdminComponentSpecs != null) {
      for (YbAdminComponentSpec spec : bundleData.ybAdminComponentSpecs) {
        spec.setYbAdminArgs(
            manifestOwned(
                "YbAdmin",
                "arguments",
                spec.getComponentName(),
                spec.getYbAdminArgs(),
                manifestMerger.ybAdminArgs(spec.getComponentName())));
      }
    }
  }

  /**
   * Timeouts come from YBA's manifest too: nothing else bounds a node command, so a request's
   * timeout could keep it, and the task waiting on it, running indefinitely.
   */
  private void useManifestTimeouts(SupportBundleFormDataV2 bundleData) {
    useManifestScriptTimeouts(bundleData.filesComponentSpecs);
    useManifestScriptTimeouts(bundleData.bashComponentSpecs);
    useManifestScriptTimeouts(bundleData.ybaComponentSpecs);
    if (bundleData.ysqlComponentSpecs != null) {
      for (YSQLComponentSpec spec : bundleData.ysqlComponentSpecs) {
        spec.setTimeoutSecs(
            manifestTimeout(
                spec.getComponentName(),
                spec.getTimeoutSecs(),
                manifestMerger.queryTimeoutSecs(spec.getComponentName())));
      }
    }
    if (bundleData.ycqlComponentSpecs != null) {
      for (YCQLComponentSpec spec : bundleData.ycqlComponentSpecs) {
        spec.setTimeoutSecs(
            manifestTimeout(
                spec.getComponentName(),
                spec.getTimeoutSecs(),
                manifestMerger.queryTimeoutSecs(spec.getComponentName())));
      }
    }
    if (bundleData.ybAdminComponentSpecs != null) {
      for (YbAdminComponentSpec spec : bundleData.ybAdminComponentSpecs) {
        spec.setTimeoutSecs(
            manifestTimeout(
                spec.getComponentName(),
                spec.getTimeoutSecs(),
                manifestMerger.queryTimeoutSecs(spec.getComponentName())));
      }
    }
  }

  private void useManifestScriptTimeouts(List<? extends ScriptTarComponentSpec> specs) {
    if (specs == null) {
      return;
    }
    for (ScriptTarComponentSpec spec : specs) {
      spec.setTimeoutSecs(
          manifestTimeout(
              spec.getComponentName(),
              spec.getTimeoutSecs(),
              manifestMerger.scriptTimeoutSecs(spec.getComponentName())));
    }
  }

  private static long manifestTimeout(String name, long requested, long manifest) {
    if (requested > 0 && requested != manifest) {
      log.warn(
          "Component '{}' runs with the {} s timeout in YBA's manifest rather than the request's"
              + " {} s",
          name,
          manifest,
          requested);
    }
    return manifest;
  }

  private static List<String> manifestOwned(
      String group,
      String what,
      String name,
      List<String> requested,
      Optional<List<String>> manifest) {
    List<String> values =
        manifest.orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "'%s' is not a %s component in the support bundle manifest.",
                        name, group)));
    if (!values.equals(requested == null ? List.of() : requested)) {
      log.warn(
          "Component '{}' runs the {} in YBA's manifest {} rather than the request's {}",
          name,
          what,
          values,
          requested);
    }
    return values;
  }

  private SupportBundleFormDataV2 toFormData(SupportBundleCreateSpec createSpec) {
    SupportBundleFormDataV2 bundleData =
        SupportBundleCreateSpecMapper.INSTANCE.toSupportBundleFormData(createSpec);
    bundleData.resolveDefaultDates(confGetter);
    return bundleData;
  }

  /** Retention days is read once per request rather than per row of the page. */
  private Function<SupportBundleV2, SupportBundle> toApi(int retentionDays) {
    return bundle -> SupportBundleMapper.INSTANCE.toApi(bundle, retentionDays);
  }

  private InputStream toInputStream(SupportBundleV2 bundle) {
    if (bundle.getStatus() != SupportBundleV2StatusType.Success) {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("No bundle found for %s", bundle.getBundleUUID()));
    }
    return SupportBundleV2.getAsInputStream(bundle.getBundleUUID());
  }

  private int retentionDays() {
    return confGetter.getStaticConf().getInt("yb.support_bundle.retention_days");
  }
}
