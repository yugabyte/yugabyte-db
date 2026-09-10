// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.verify;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.PromExportType;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.text.SimpleDateFormat;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.util.ArrayList;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Result;

public class SupportBundleApiControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private Universe universe;
  private RuntimeConfGetter confGetter;
  private UUID fakeTaskUUID;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("test-universe-v2", customer.getId());
    user = ModelFactory.testSuperAdminUserNewRbac(customer);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    // CustomerTask rows carry a foreign key onto task_info, so the task has to really exist.
    fakeTaskUUID = buildTaskInfo(null, TaskType.CreateSupportBundleV2);
    lenient()
        .when(mockCommissioner.submit(any(TaskType.class), any(SupportBundleTaskParamsV2.class)))
        .thenReturn(fakeTaskUUID);
  }

  /* ==== Helper Request Functions ==== */

  private Result pageListSupportBundles(UUID customerUUID, UUID universeUUID, JsonNode body) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format(
            "/api/v2/customers/%s/universes/%s/support-bundles/page", customerUUID, universeUUID),
        user.createAuthToken(),
        body);
  }

  private Result pageListYbaSupportBundles(UUID customerUUID, JsonNode body) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format("/api/v2/customers/%s/support-bundles/yba/page", customerUUID),
        user.createAuthToken(),
        body);
  }

  private Result getSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    return doRequestWithAuthToken(
        "GET",
        String.format(
            "/api/v2/customers/%s/universes/%s/support-bundles/%s",
            customerUUID, universeUUID, bundleUUID),
        user.createAuthToken());
  }

  private Result deleteSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    return doRequestWithAuthToken(
        "DELETE",
        String.format(
            "/api/v2/customers/%s/universes/%s/support-bundles/%s",
            customerUUID, universeUUID, bundleUUID),
        user.createAuthToken());
  }

  private Result downloadSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    return doRequestWithAuthToken(
        "GET",
        String.format(
            "/api/v2/customers/%s/universes/%s/support-bundles/%s/download",
            customerUUID, universeUUID, bundleUUID),
        user.createAuthToken());
  }

  private Result getYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    return doRequestWithAuthToken(
        "GET",
        String.format("/api/v2/customers/%s/support-bundles/yba/%s", customerUUID, bundleUUID),
        user.createAuthToken());
  }

  private Result deleteYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    return doRequestWithAuthToken(
        "DELETE",
        String.format("/api/v2/customers/%s/support-bundles/yba/%s", customerUUID, bundleUUID),
        user.createAuthToken());
  }

  private Result downloadYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    return doRequestWithAuthToken(
        "GET",
        String.format(
            "/api/v2/customers/%s/support-bundles/yba/%s/download", customerUUID, bundleUUID),
        user.createAuthToken());
  }

  private Result listSupportBundleComponents(UUID customerUUID) {
    return doRequestWithAuthToken(
        "GET",
        String.format("/api/v2/customers/%s/support-bundle/components", customerUUID),
        user.createAuthToken());
  }

  private Result createSupportBundle(UUID customerUUID, UUID universeUUID, JsonNode body) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format(
            "/api/v2/customers/%s/universes/%s/support-bundles", customerUUID, universeUUID),
        user.createAuthToken(),
        body);
  }

  private Result createYbaSupportBundle(UUID customerUUID, JsonNode body) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format("/api/v2/customers/%s/support-bundles/yba", customerUUID),
        user.createAuthToken(),
        body);
  }

  private Result estimateSupportBundleSize(UUID customerUUID, UUID universeUUID, JsonNode body) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format(
            "/api/v2/customers/%s/universes/%s/support-bundles/estimate-size",
            customerUUID, universeUUID),
        user.createAuthToken(),
        body);
  }

  private Result estimateYbaSupportBundleSize(UUID customerUUID, JsonNode body) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format("/api/v2/customers/%s/support-bundles/yba/estimate-size", customerUUID),
        user.createAuthToken(),
        body);
  }

  /* ==== Assertion Helpers ==== */

  private int retentionDays() {
    return confGetter.getStaticConf().getInt("yb.support_bundle.retention_days");
  }

  /** Compares dates by instant so the assertion does not depend on the serialized offset. */
  private Instant instantOf(Date date) {
    return date.toInstant();
  }

  private Instant instantOf(JsonNode dateNode) {
    return OffsetDateTime.parse(dateNode.asText()).toInstant();
  }

  /* ==== Fixtures ==== */

  private SupportBundleFormDataV2 bundleFormData() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = new Date();
    bundleData.endDate = new Date();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    return bundleData;
  }

  /** Mirrors the on-disk name produced by the collection task. */
  private String fakeBundlePath(String scopeName) {
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    return String.format(
        "/tmp/yugaware_tests/support_bundle_v2/yb-support-bundle-%s-%s-logs.tar.gz",
        scopeName, datePrefix);
  }

  private SupportBundleV2 createUniverseBundle(SupportBundleV2StatusType status) {
    return createUniverseBundle(universe, status);
  }

  private SupportBundleV2 createUniverseBundle(Universe scope, SupportBundleV2StatusType status) {
    return saveBundle(
        SupportBundleV2.create(bundleFormData(), scope, confGetter), status, scope.getName());
  }

  private SupportBundleV2 createYbaBundle(Customer owner, SupportBundleV2StatusType status) {
    return saveBundle(
        SupportBundleV2.createYbaOnly(bundleFormData(), owner, confGetter), status, "yba");
  }

  private SupportBundleV2 saveBundle(
      SupportBundleV2 bundle, SupportBundleV2StatusType status, String scopeName) {
    bundle.setStatus(status);
    // Only a bundle that finished collecting has been written to disk.
    if (status == SupportBundleV2StatusType.Success) {
      bundle.setPath(fakeBundlePath(scopeName));
    }
    bundle.save();
    return bundle;
  }

  /**
   * Creation date drives page ordering, and bundles created back to back can land in the same
   * millisecond, so anything asserting on order has to pin the dates rather than take "now".
   */
  private SupportBundleV2 createUniverseBundleCreatedAt(String creationDate) {
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Success);
    bundle.setCreationDate(Date.from(Instant.parse(creationDate)));
    bundle.save();
    return bundle;
  }

  /** Reads the row back so assertions compare against what the database actually stored. */
  private SupportBundleV2 reload(SupportBundleV2 bundle) {
    return SupportBundleV2.getOrNotFound(bundle.getBundleUUID());
  }

  private List<String> pagedUuids(Result result) {
    JsonNode entities = Json.parse(contentAsString(result)).get("entities");
    List<String> uuids = new ArrayList<>();
    entities.forEach(entity -> uuids.add(entity.get("info").get("uuid").asText()));
    return uuids;
  }

  private ObjectNode pageRequest(Integer offset, Integer limit, String direction) {
    ObjectNode body = Json.newObject();
    if (offset != null) {
      body.put("offset", offset);
    }
    if (limit != null) {
      body.put("limit", limit);
    }
    if (direction != null) {
      body.put("direction", direction);
    }
    return body;
  }

  /**
   * Minimal request body that passes validation. Written as raw snake_case JSON rather than through
   * the generated model so that the published wire contract is what gets exercised.
   */
  private ObjectNode ybaComponentRequest() {
    ObjectNode componentSpec = Json.newObject();
    componentSpec.put("component_name", "CustomYbaScript");
    componentSpec.put("script_path", "bin/yba_utils.sh");
    componentSpec.put("remote_tar_path", "/tmp/custom-yba-script.tar.gz");
    componentSpec.set("params", Json.newArray().add("create_custom_yba_script_bundle"));

    ObjectNode body = Json.newObject();
    body.set("components", Json.newArray().add("YBAComponent"));
    body.set("yba_component_specs", Json.newArray().add(componentSpec));
    return body;
  }

  /** The form the request was mapped onto, as handed to the collection task. */
  private SupportBundleFormDataV2 submittedBundleData() {
    ArgumentCaptor<SupportBundleTaskParamsV2> params =
        ArgumentCaptor.forClass(SupportBundleTaskParamsV2.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateSupportBundleV2), params.capture());
    return params.getValue().bundleData;
  }

  /* ==== Create Support Bundle ==== */

  @Test
  public void testCreateSupportBundleMapsSnakeCaseRequest() {
    ObjectNode body = ybaComponentRequest();
    body.set(
        "components",
        Json.newArray().add("YBAComponent").add("YbaMetadata").add("ApplicationLogs"));
    body.put("start_date", "2024-01-01T00:00:00Z");
    body.put("end_date", "2024-01-02T00:00:00Z");
    body.put("max_core_file_size", 4096);
    body.put("max_num_recent_cores", 3);
    body.put("filter_pg_audit_logs", true);
    body.put("prom_export_type", "REMOTE_READ");
    body.put("prom_metrics_format", "PROM_CHUNK");
    body.put("prom_dump_down_sample", false);
    body.put("step_prom_dump_secs", 30);
    body.put("prom_dump_start_date", "2024-01-01T06:00:00Z");
    body.put("prom_dump_end_date", "2024-01-01T12:00:00Z");
    body.set("prometheus_metrics_types", Json.newArray().add("MASTER_EXPORT").add("NODE_EXPORT"));

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), body);
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(fakeTaskUUID.toString(), json.get("task_uuid").asText());
    SupportBundleV2 created =
        SupportBundleV2.getOrNotFound(UUID.fromString(json.get("resource_uuid").asText()));
    assertEquals(universe.getUniverseUUID(), created.getScopeUUID());

    SupportBundleFormDataV2 bundleData = submittedBundleData();
    assertEquals(
        EnumSet.of(
            ComponentType.YBAComponent, ComponentType.YbaMetadata, ComponentType.ApplicationLogs),
        bundleData.components);
    assertEquals("CustomYbaScript", bundleData.ybaComponentSpecs.get(0).getComponentName());
    assertEquals("bin/yba_utils.sh", bundleData.ybaComponentSpecs.get(0).getScriptPath());
    assertEquals(
        List.of("create_custom_yba_script_bundle"),
        bundleData.ybaComponentSpecs.get(0).getParams());
    assertEquals(Instant.parse("2024-01-01T00:00:00Z"), instantOf(bundleData.startDate));
    assertEquals(Instant.parse("2024-01-02T00:00:00Z"), instantOf(bundleData.endDate));
    assertEquals(Instant.parse("2024-01-01T06:00:00Z"), instantOf(bundleData.promDumpStartDate));
    assertEquals(Instant.parse("2024-01-01T12:00:00Z"), instantOf(bundleData.promDumpEndDate));
    assertEquals(4096L, bundleData.maxCoreFileSize);
    assertEquals(3, bundleData.maxNumRecentCores);
    assertTrue(bundleData.filterPgAuditLogs);
    assertFalse(bundleData.promDumpDownSample);
    assertEquals(PromExportType.REMOTE_READ, bundleData.promExportType);
    assertEquals(PrometheusMetricsFormat.PROM_CHUNK, bundleData.promMetricsFormat);
    assertEquals(Integer.valueOf(30), bundleData.stepPromDumpSecs);
    assertEquals(
        EnumSet.of(PrometheusMetricsType.MASTER_EXPORT, PrometheusMetricsType.NODE_EXPORT),
        bundleData.prometheusMetricsTypes);
  }

  /**
   * Anything the request leaves out has to fall back to the form's own defaults, which is why the
   * mapper fills an already-constructed form instead of letting MapStruct instantiate one.
   */
  @Test
  public void testCreateSupportBundlePreservesFormDefaults() {
    Result result =
        createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), ybaComponentRequest());
    assertEquals(OK, result.status());

    SupportBundleFormDataV2 bundleData = submittedBundleData();
    assertEquals(1, bundleData.maxNumRecentCores);
    assertEquals(25000000000L, bundleData.maxCoreFileSize);
    assertEquals(PromExportType.PROMQL, bundleData.promExportType);
    assertEquals(PrometheusMetricsFormat.PROMQL_JSON, bundleData.promMetricsFormat);
    assertEquals(PrometheusMetricsFormat.PROM_CHUNK, bundleData.paMetricsFormat);
    assertTrue(bundleData.promDumpDownSample);
    assertFalse(bundleData.filterPgAuditLogs);
    assertTrue(bundleData.prometheusMetricsTypes.isEmpty());
    assertTrue(bundleData.promQueries.isEmpty());
    // Omitted dates are resolved server side into a trailing 24 hour window.
    assertEquals(
        TimeUnit.HOURS.toMillis(24), bundleData.endDate.getTime() - bundleData.startDate.getTime());
  }

  /** Creates are audited from the spec now, so no audit entry means the metadata regressed. */
  @Test
  public void testCreateSupportBundleIsAudited() {
    Result result =
        createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), ybaComponentRequest());
    assertEquals(OK, result.status());

    List<Audit> audits = Audit.getAll(customer.getUuid());
    assertEquals(1, audits.size());
    assertEquals(Audit.TargetType.SupportBundle, audits.get(0).getTarget());
    assertEquals(Audit.ActionType.Create, audits.get(0).getAction());
    assertEquals(fakeTaskUUID, audits.get(0).getTaskUUID());
  }

  /* ==== Node selection ==== */

  /** Adds named nodes to the universe so node selection has something to match against. */
  private List<String> addUniverseNodes(String... nodeNames) {
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.nodeDetailsSet = new HashSet<>();
          for (String nodeName : nodeNames) {
            NodeDetails node = new NodeDetails();
            node.nodeName = nodeName;
            node.placementUuid = details.getPrimaryCluster().uuid;
            details.nodeDetailsSet.add(node);
          }
          u.setUniverseDetails(details);
        });
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    return List.of(nodeNames);
  }

  @Test
  public void testCreateSupportBundleWithMatchingNodeNames() {
    addUniverseNodes("node-1", "node-2", "node-3");
    ObjectNode body = ybaComponentRequest();
    body.set("node_names", Json.newArray().add("node-1").add("node-3"));

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), body);
    assertEquals(OK, result.status());

    assertEquals(List.of("node-1", "node-3"), submittedBundleData().nodeNames);
    SupportBundleV2 created =
        SupportBundleV2.getOrNotFound(
            UUID.fromString(Json.parse(contentAsString(result)).get("resource_uuid").asText()));
    assertEquals(List.of("node-1", "node-3"), created.getBundleDetails().getNodeNames());
  }

  /**
   * A single stale name should not block collection from the nodes that do exist, but it must not
   * survive into what gets persisted either, so the bundle manifest names only real targets.
   */
  @Test
  public void testCreateSupportBundleNarrowsPartiallyMatchingNodeNames() {
    addUniverseNodes("node-1", "node-2");
    ObjectNode body = ybaComponentRequest();
    body.set("node_names", Json.newArray().add("node-1").add("gone-node"));

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), body);
    assertEquals(OK, result.status());

    assertEquals(List.of("node-1"), submittedBundleData().nodeNames);
    SupportBundleV2 created =
        SupportBundleV2.getOrNotFound(
            UUID.fromString(Json.parse(contentAsString(result)).get("resource_uuid").asText()));
    assertEquals(List.of("node-1"), created.getBundleDetails().getNodeNames());
  }

  @Test
  public void testCreateSupportBundleRejectsFullyUnmatchedNodeNames() {
    addUniverseNodes("node-1", "node-2");
    ObjectNode body = ybaComponentRequest();
    body.set("node_names", Json.newArray().add("gone-1").add("gone-2"));

    Result result =
        assertPlatformException(
            () -> createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testCreateSupportBundleRejectsSingleUnmatchedNodeName() {
    addUniverseNodes("node-1", "node-2");
    ObjectNode body = ybaComponentRequest();
    body.set("node_names", Json.newArray().add("gone-1"));

    Result result =
        assertPlatformException(
            () -> createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  /** Omitting the list has to keep meaning "every node", or existing callers change behaviour. */
  @Test
  public void testCreateSupportBundleWithoutNodeNamesLeavesSelectionUnset() {
    addUniverseNodes("node-1", "node-2");

    Result result =
        createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), ybaComponentRequest());
    assertEquals(OK, result.status());

    assertNull(submittedBundleData().nodeNames);
  }

  @Test
  public void testEstimateSupportBundleSizeRejectsUnmatchedNodeNames() {
    addUniverseNodes("node-1");
    ObjectNode body = ybaComponentRequest();
    body.set("node_names", Json.newArray().add("gone-1"));

    Result result =
        assertPlatformException(
            () -> estimateSupportBundleSize(customer.getUuid(), universe.getUniverseUUID(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  /** A YBA-only bundle has no universe, so there is nothing for a node name to refer to. */
  @Test
  public void testCreateYbaSupportBundleRejectsNodeNames() {
    ObjectNode body = ybaComponentRequest();
    body.set("node_names", Json.newArray().add("node-1"));

    Result result = assertPlatformException(() -> createYbaSupportBundle(customer.getUuid(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testCreateSupportBundleWithoutRequiredSpecs() {
    ObjectNode body = ybaComponentRequest();
    body.remove("yba_component_specs");

    Result result =
        assertPlatformException(
            () -> createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  /* ==== Create YBA-only Support Bundle ==== */

  @Test
  public void testCreateYbaSupportBundle() {
    Result result = createYbaSupportBundle(customer.getUuid(), ybaComponentRequest());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(fakeTaskUUID.toString(), json.get("task_uuid").asText());
    SupportBundleV2 created =
        SupportBundleV2.getOrNotFound(UUID.fromString(json.get("resource_uuid").asText()));
    assertTrue(created.isYbaOnly());
    assertEquals(customer.getUuid(), created.getCustomerUUID());

    assertEquals(EnumSet.of(ComponentType.YBAComponent), submittedBundleData().components);
  }

  /** A YBA-only bundle has no universe, so node-level and universe-scoped components cannot run. */
  @Test
  public void testCreateYbaSupportBundleRejectsUniverseComponents() {
    ObjectNode body = ybaComponentRequest();
    body.set("components", Json.newArray().add("YBAComponent").add("UniverseLogs"));

    Result result = assertPlatformException(() -> createYbaSupportBundle(customer.getUuid(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  /* ==== Estimate Support Bundle Size ==== */

  @Test
  public void testEstimateSupportBundleSize() {
    Result result =
        estimateSupportBundleSize(
            customer.getUuid(), universe.getUniverseUUID(), ybaComponentRequest());
    assertEquals(OK, result.status());

    // No node is reachable under test and the YBA component reports no files, so every
    // requested component estimates to zero; the point here is the shape of the response.
    JsonNode data = Json.parse(contentAsString(result)).get("data");
    assertEquals(0, data.get("YBA").get("YBAComponent").asLong());
  }

  /**
   * The prom dump window must be given as a pair. Rejecting a half-specified one proves the
   * snake_case prom fields really do reach the form, since two nulls would validate cleanly.
   */
  @Test
  public void testEstimateSupportBundleSizeWithHalfOpenPromWindow() {
    ObjectNode body = ybaComponentRequest();
    body.set("components", Json.newArray().add("PrometheusMetrics"));
    body.put("prom_dump_start_date", "2024-01-01T06:00:00Z");

    Result result =
        assertPlatformException(
            () -> estimateSupportBundleSize(customer.getUuid(), universe.getUniverseUUID(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  /* ==== Estimate YBA-only Support Bundle Size ==== */

  @Test
  public void testEstimateYbaSupportBundleSize() {
    Result result = estimateYbaSupportBundleSize(customer.getUuid(), ybaComponentRequest());
    assertEquals(OK, result.status());

    // The estimate itself is a placeholder until the YBA component lands, so this only pins
    // down that the endpoint is routed and returns the estimate envelope.
    JsonNode data = Json.parse(contentAsString(result)).get("data");
    assertTrue(data.isObject());
  }

  /** A YBA-only bundle has no universe, so node-level and universe-scoped components cannot run. */
  @Test
  public void testEstimateYbaSupportBundleSizeRejectsUniverseComponents() {
    ObjectNode body = ybaComponentRequest();
    body.set("components", Json.newArray().add("YBAComponent").add("UniverseLogs"));

    Result result =
        assertPlatformException(() -> estimateYbaSupportBundleSize(customer.getUuid(), body));
    assertEquals(BAD_REQUEST, result.status());
  }

  /* ==== Get Support Bundle ==== */

  @Test
  public void testGetSupportBundle() {
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Success);

    Result result =
        getSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(bundle.getBundleUUID().toString(), json.get("info").get("uuid").asText());
    assertEquals("Success", json.get("info").get("status").asText());
    assertEquals(
        universe.getUniverseUUID().toString(), json.get("info").get("scope_uuid").asText());
    assertEquals("YBAComponent", json.get("spec").get("components").get(0).asText());
    // The archive itself was never written, so it contributes no size.
    assertEquals(0, json.get("info").get("size_in_bytes").asLong());
    Date creationDate = reload(bundle).getCreationDate();
    assertEquals(instantOf(creationDate), instantOf(json.get("info").get("creation_date")));
    assertEquals(
        instantOf(new SupportBundleUtil().getDateNDaysAfter(creationDate, retentionDays())),
        instantOf(json.get("info").get("expiration_date")));
  }

  /**
   * The node selection has to survive onto the read model, otherwise a two node bundle taken from a
   * thirty node universe is indistinguishable from one where twenty eight nodes silently failed.
   */
  @Test
  public void testGetSupportBundleReturnsNodeNames() {
    addUniverseNodes("node-1", "node-2");
    ObjectNode body = ybaComponentRequest();
    body.set("node_names", Json.newArray().add("node-1"));
    Result created = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), body);
    assertEquals(OK, created.status());
    UUID bundleUUID =
        UUID.fromString(Json.parse(contentAsString(created)).get("resource_uuid").asText());

    Result result = getSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bundleUUID);
    assertEquals(OK, result.status());

    JsonNode nodeNames = Json.parse(contentAsString(result)).get("spec").get("node_names");
    assertEquals(1, nodeNames.size());
    assertEquals("node-1", nodeNames.get(0).asText());
  }

  /** A bundle collected from every node reports no selection rather than listing them all. */
  @Test
  public void testGetSupportBundleWithoutNodeSelectionOmitsNodeNames() {
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Success);

    Result result =
        getSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID());
    assertEquals(OK, result.status());

    JsonNode spec = Json.parse(contentAsString(result)).get("spec");
    assertTrue(spec.get("node_names") == null || spec.get("node_names").isNull());
  }

  /**
   * A bundle is stamped with a creation date as soon as it is requested, but only expires once it
   * has succeeded and therefore actually occupies disk.
   */
  @Test
  public void testGetIncompleteSupportBundleHasNoExpirationDate() {
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Running);

    Result result =
        getSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID());
    assertEquals(OK, result.status());

    JsonNode info = Json.parse(contentAsString(result)).get("info");
    assertEquals(instantOf(reload(bundle).getCreationDate()), instantOf(info.get("creation_date")));
    // Null properties are dropped from v2 responses rather than serialized as null.
    assertFalse(info.has("expiration_date"));
    assertEquals(0, info.get("size_in_bytes").asLong());
  }

  @Test
  public void testGetSupportBundleWithUnknownBundle() {
    Result result =
        assertPlatformException(
            () ->
                getSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  @Test
  public void testGetSupportBundleWithUnknownUniverse() {
    Result result =
        assertPlatformException(
            () -> getSupportBundle(customer.getUuid(), UUID.randomUUID(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  @Test
  public void testGetSupportBundleWithUniverseOfAnotherCustomer() {
    Customer otherCustomer = ModelFactory.testCustomer("tc2", "other@customer.com");
    Universe otherUniverse =
        ModelFactory.createUniverse("other-universe-v2", otherCustomer.getId());
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Success);

    Result result =
        assertPlatformException(
            () ->
                getSupportBundle(
                    customer.getUuid(), otherUniverse.getUniverseUUID(), bundle.getBundleUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  /**
   * Authorization only covers the universe in the path, so the bundle itself has to be tied back to
   * it. Both universes belong to the caller here, which is what makes this distinct from
   * testGetSupportBundleWithUniverseOfAnotherCustomer.
   */
  @Test
  public void testGetSupportBundleOfAnotherUniverseOfSameCustomer() {
    Universe otherUniverse = ModelFactory.createUniverse("sibling-universe-v2", customer.getId());
    SupportBundleV2 bundle = createUniverseBundle(otherUniverse, SupportBundleV2StatusType.Success);

    Result result =
        assertPlatformException(
            () ->
                getSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  /* ==== Page List Support Bundles ==== */

  @Test
  public void testPageListSupportBundles() {
    createUniverseBundle(SupportBundleV2StatusType.Success);
    createUniverseBundle(SupportBundleV2StatusType.Running);

    Result result =
        pageListSupportBundles(customer.getUuid(), universe.getUniverseUUID(), Json.newObject());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(2, json.get("total_count").asInt());
    assertEquals(2, json.get("entities").size());
    assertFalse(json.get("has_next").asBoolean());
    assertFalse(json.get("has_prev").asBoolean());
  }

  @Test
  public void testPageListSupportBundlesWithUnknownUniverse() {
    Result result =
        assertPlatformException(
            () -> pageListSupportBundles(customer.getUuid(), UUID.randomUUID(), Json.newObject()));
    assertEquals(NOT_FOUND, result.status());
  }

  /** Ascending is the default, so listing newest first has to be asked for explicitly. */
  @Test
  public void testPageListSupportBundlesOrdersByCreationDate() {
    SupportBundleV2 oldest = createUniverseBundleCreatedAt("2024-01-01T00:00:00Z");
    SupportBundleV2 middle = createUniverseBundleCreatedAt("2024-02-01T00:00:00Z");
    SupportBundleV2 newest = createUniverseBundleCreatedAt("2024-03-01T00:00:00Z");

    Result ascending =
        pageListSupportBundles(
            customer.getUuid(), universe.getUniverseUUID(), pageRequest(null, null, "ASC"));
    assertEquals(OK, ascending.status());
    assertEquals(
        List.of(
            oldest.getBundleUUID().toString(),
            middle.getBundleUUID().toString(),
            newest.getBundleUUID().toString()),
        pagedUuids(ascending));

    Result descending =
        pageListSupportBundles(
            customer.getUuid(), universe.getUniverseUUID(), pageRequest(null, null, "DESC"));
    assertEquals(OK, descending.status());
    assertEquals(
        List.of(
            newest.getBundleUUID().toString(),
            middle.getBundleUUID().toString(),
            oldest.getBundleUUID().toString()),
        pagedUuids(descending));
  }

  @Test
  public void testPageListSupportBundlesHonorsOffsetAndLimit() {
    createUniverseBundleCreatedAt("2024-01-01T00:00:00Z");
    SupportBundleV2 middle = createUniverseBundleCreatedAt("2024-02-01T00:00:00Z");
    createUniverseBundleCreatedAt("2024-03-01T00:00:00Z");

    Result result =
        pageListSupportBundles(
            customer.getUuid(), universe.getUniverseUUID(), pageRequest(1, 1, "ASC"));
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    // total_count counts every matching row, not just the ones on this page.
    assertEquals(3, json.get("total_count").asInt());
    assertTrue(json.get("has_prev").asBoolean());
    assertTrue(json.get("has_next").asBoolean());
    assertEquals(List.of(middle.getBundleUUID().toString()), pagedUuids(result));
  }

  @Test
  public void testPageListSupportBundlesWithLimitOutOfBounds() {
    for (int limit : new int[] {0, 501}) {
      Result result =
          assertPlatformException(
              () ->
                  pageListSupportBundles(
                      customer.getUuid(),
                      universe.getUniverseUUID(),
                      pageRequest(null, limit, null)));
      assertEquals(BAD_REQUEST, result.status());
    }
  }

  /** A sibling universe's bundles belong to the caller but must not leak into this page. */
  @Test
  public void testPageListSupportBundlesExcludesOtherUniverses() {
    Universe otherUniverse = ModelFactory.createUniverse("sibling-universe-v2", customer.getId());
    createUniverseBundle(otherUniverse, SupportBundleV2StatusType.Success);
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Success);

    Result result =
        pageListSupportBundles(customer.getUuid(), universe.getUniverseUUID(), Json.newObject());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(1, json.get("total_count").asInt());
    assertEquals(List.of(bundle.getBundleUUID().toString()), pagedUuids(result));
  }

  /* ==== Delete Support Bundle ==== */

  @Test
  public void testDeleteSupportBundleWithUnknownBundle() {
    Result result =
        assertPlatformException(
            () ->
                deleteSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  /** A bundle that is still being collected is a state conflict, not a missing resource. */
  @Test
  public void testDeleteRunningSupportBundleReturnsConflict() {
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Running);

    Result result =
        assertPlatformException(
            () ->
                deleteSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID()));
    assertEquals(CONFLICT, result.status());
  }

  @Test
  public void testDeleteSupportBundleOfAnotherUniverseOfSameCustomer() {
    Universe otherUniverse = ModelFactory.createUniverse("sibling-universe-v2", customer.getId());
    SupportBundleV2 bundle = createUniverseBundle(otherUniverse, SupportBundleV2StatusType.Success);

    Result result =
        assertPlatformException(
            () ->
                deleteSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  /* ==== Download Support Bundle ==== */

  @Test
  public void testDownloadSupportBundleWithUnknownBundle() {
    Result result =
        assertPlatformException(
            () ->
                downloadSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  @Test
  public void testDownloadIncompleteSupportBundle() {
    SupportBundleV2 bundle = createUniverseBundle(SupportBundleV2StatusType.Running);

    Result result =
        assertPlatformException(
            () ->
                downloadSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  @Test
  public void testDownloadSupportBundleOfAnotherUniverseOfSameCustomer() {
    Universe otherUniverse = ModelFactory.createUniverse("sibling-universe-v2", customer.getId());
    SupportBundleV2 bundle = createUniverseBundle(otherUniverse, SupportBundleV2StatusType.Success);

    Result result =
        assertPlatformException(
            () ->
                downloadSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), bundle.getBundleUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  /* ==== YBA-only Support Bundles ==== */

  @Test
  public void testGetYbaSupportBundle() {
    SupportBundleV2 bundle = createYbaBundle(customer, SupportBundleV2StatusType.Success);

    Result result = getYbaSupportBundle(customer.getUuid(), bundle.getBundleUUID());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(bundle.getBundleUUID().toString(), json.get("info").get("uuid").asText());
    assertEquals(customer.getUuid().toString(), json.get("info").get("customer_uuid").asText());
  }

  @Test
  public void testGetYbaSupportBundleWithUnknownBundle() {
    Result result =
        assertPlatformException(() -> getYbaSupportBundle(customer.getUuid(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  /** A bundle owned by another customer must not be distinguishable from one that never existed. */
  @Test
  public void testGetYbaSupportBundleOfAnotherCustomer() {
    Customer otherCustomer = ModelFactory.testCustomer("tc2", "other@customer.com");
    SupportBundleV2 bundle = createYbaBundle(otherCustomer, SupportBundleV2StatusType.Success);

    Result result =
        assertPlatformException(
            () -> getYbaSupportBundle(customer.getUuid(), bundle.getBundleUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  @Test
  public void testDeleteRunningYbaSupportBundleReturnsConflict() {
    SupportBundleV2 bundle = createYbaBundle(customer, SupportBundleV2StatusType.Running);

    Result result =
        assertPlatformException(
            () -> deleteYbaSupportBundle(customer.getUuid(), bundle.getBundleUUID()));
    assertEquals(CONFLICT, result.status());
  }

  @Test
  public void testDeleteYbaSupportBundleWithUnknownBundle() {
    Result result =
        assertPlatformException(
            () -> deleteYbaSupportBundle(customer.getUuid(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  @Test
  public void testDownloadYbaSupportBundleWithUnknownBundle() {
    Result result =
        assertPlatformException(
            () -> downloadYbaSupportBundle(customer.getUuid(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, result.status());
  }

  /**
   * The YBA-only page is scoped to the calling customer, and a universe bundle is not YBA-only even
   * when the caller owns the universe.
   */
  @Test
  public void testPageListYbaSupportBundles() {
    Customer otherCustomer = ModelFactory.testCustomer("tc2", "other@customer.com");
    createYbaBundle(otherCustomer, SupportBundleV2StatusType.Success);
    createUniverseBundle(SupportBundleV2StatusType.Success);
    SupportBundleV2 bundle = createYbaBundle(customer, SupportBundleV2StatusType.Success);

    Result result = pageListYbaSupportBundles(customer.getUuid(), Json.newObject());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(1, json.get("total_count").asInt());
    assertFalse(json.get("has_next").asBoolean());
    assertEquals(List.of(bundle.getBundleUUID().toString()), pagedUuids(result));
  }

  @Test
  public void testPageListYbaSupportBundlesHonorsLimit() {
    createYbaBundle(customer, SupportBundleV2StatusType.Success);
    createYbaBundle(customer, SupportBundleV2StatusType.Running);

    Result result = pageListYbaSupportBundles(customer.getUuid(), pageRequest(null, 1, null));
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(2, json.get("total_count").asInt());
    assertEquals(1, json.get("entities").size());
    assertTrue(json.get("has_next").asBoolean());
    assertFalse(json.get("has_prev").asBoolean());
  }

  /** The page is reachable only for the caller's own customer, which authorization enforces. */
  @Test
  public void testPageListYbaSupportBundlesOfAnotherCustomer() {
    Customer otherCustomer = ModelFactory.testCustomer("tc2", "other@customer.com");
    createYbaBundle(otherCustomer, SupportBundleV2StatusType.Success);

    Result result = pageListYbaSupportBundles(otherCustomer.getUuid(), Json.newObject());
    assertEquals(UNAUTHORIZED, result.status());
  }

  /* ==== List Support Bundle Components ==== */

  @Test
  public void testListSupportBundleComponents() {
    Result result = listSupportBundleComponents(customer.getUuid());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(ComponentType.values().length, json.size());
  }
}
