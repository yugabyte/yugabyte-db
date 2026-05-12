// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.models.CollectFilesRequest;
import api.v2.models.CollectFilesResponse;
import api.v2.models.FileCollectionOptions;
import api.v2.models.NodeSelection;
import api.v2.models.RunScriptRequest;
import api.v2.models.RunScriptResponse;
import api.v2.models.ScriptOptions;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.FileCollectionDownloader;
import com.yugabyte.yw.common.LocalhostAccessChecker;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeFileCollector;
import com.yugabyte.yw.common.NodeScriptRunner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import play.mvc.Http.Request;
import play.test.Helpers;

/**
 * Unit tests for Node Troubleshooting APIs: - POST /run-script (runScript) - POST /file-collections
 * (createFileCollection) - GET /file-collections/{collectionUUID}/download (downloadFileCollection)
 * - DELETE /file-collections/{collectionUUID} (deleteFileCollection)
 */
@RunWith(JUnitParamsRunner.class)
public class NodeTroubleshootingApiTest extends FakeDBApplication {

  private Universe defaultUniverse;
  private Customer defaultCustomer;

  @InjectMocks private UniverseManagementHandler handler;

  @Mock private RuntimeConfGetter mockConfGetter;
  @Mock private NodeFileCollector mockNodeFileCollector;
  @Mock private FileCollectionDownloader mockFileCollectionDownloader;
  @Mock private LocalhostAccessChecker mockLocalhostChecker;
  @Mock private NodeScriptRunner mockNodeScriptRunner;
  @Mock private AuditService mockAuditService;

  @Before
  public void setUp() throws Exception {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse =
        ModelFactory.createUniverse("test-universe", defaultCustomer.getId(), Common.CloudType.gcp);
    MockitoAnnotations.openMocks(this);

    // Inject AuditService into parent class ApiControllerUtils
    Field auditServiceField = handler.getClass().getSuperclass().getDeclaredField("auditService");
    auditServiceField.setAccessible(true);
    auditServiceField.set(handler, mockAuditService);
  }

  private Request createLocalhostRequest(String method, String path) {
    return Helpers.fakeRequest(method, path).remoteAddress("127.0.0.1").build();
  }

  private Request createRemoteRequest(String method, String path) {
    return Helpers.fakeRequest(method, path).remoteAddress("192.168.1.100").build();
  }

  // ==================== RUN SCRIPT TESTS ====================

  @Test
  public void testRunScript_Success() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    scriptOptions.setScriptContent("echo 'Hello World'");
    scriptOptions.setTimeoutSecs(60L);
    request.setScriptOptions(scriptOptions);

    NodeScriptRunner.ExecutionResult mockResult = mock(NodeScriptRunner.ExecutionResult.class);
    when(mockResult.getTotalNodes()).thenReturn(3);
    when(mockResult.getSuccessfulNodes()).thenReturn(3);
    when(mockResult.getFailedNodes()).thenReturn(0);
    when(mockResult.isAllSucceeded()).thenReturn(true);
    when(mockResult.getTotalExecutionTimeMs()).thenReturn(500L);
    when(mockResult.getNodeResults()).thenReturn(new LinkedHashMap<>());

    when(mockNodeScriptRunner.runScript(any(Universe.class), any(), any())).thenReturn(mockResult);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute
    RunScriptResponse response =
        handler.runScript(
            httpRequest, defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID(), request);

    // Verify
    assertNotNull(response);
    assertNotNull(response.getSummary());
    assertEquals(Integer.valueOf(3), response.getSummary().getTotalNodes());
    assertEquals(Integer.valueOf(3), response.getSummary().getSuccessfulNodes());
    assertEquals(Boolean.TRUE, response.getSummary().getAllSucceeded());
  }

  @Test
  public void testRunScript_FeatureDisabled() {
    // Setup - feature is disabled
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(false);

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    scriptOptions.setScriptContent("echo 'test'");
    request.setScriptOptions(scriptOptions);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute & Verify
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.runScript(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    request));

    assertTrue(exception.getMessage().contains("not enabled"));
  }

  @Test
  public void testRunScript_LocalhostRequired() {
    // Setup - simulate localhost check failure
    doThrow(new PlatformServiceException(403, "Must be accessed from localhost"))
        .when(mockLocalhostChecker)
        .checkLocalhost(any(Request.class));

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    scriptOptions.setScriptContent("echo 'test'");
    request.setScriptOptions(scriptOptions);

    Request httpRequest =
        createRemoteRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.runScript(
                httpRequest,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                request));
  }

  @Test
  public void testRunScript_MissingScriptOptions() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    RunScriptRequest request = new RunScriptRequest();
    // No script options set

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute & Verify
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.runScript(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    request));

    assertTrue(exception.getMessage().contains("script_options is required"));
  }

  @Test
  public void testRunScript_MissingScriptContent() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    // Neither script_content nor script_file set
    request.setScriptOptions(scriptOptions);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute & Verify
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.runScript(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    request));

    assertTrue(exception.getMessage().contains("script_content or script_file must be provided"));
  }

  @Test
  public void testRunScript_BothScriptContentAndFile() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    scriptOptions.setScriptContent("echo 'inline'");
    scriptOptions.setScriptFile("/path/to/script.sh");
    request.setScriptOptions(scriptOptions);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute & Verify
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.runScript(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    request));

    assertTrue(exception.getMessage().contains("Only one of"));
  }

  @Test
  public void testRunScript_WithNodeSelection() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    scriptOptions.setScriptContent("echo 'test'");
    request.setScriptOptions(scriptOptions);

    NodeSelection nodeSelection = new NodeSelection();
    nodeSelection.setMastersOnly(true);
    request.setNodes(nodeSelection);

    NodeScriptRunner.ExecutionResult mockResult = mock(NodeScriptRunner.ExecutionResult.class);
    when(mockResult.getTotalNodes()).thenReturn(3);
    when(mockResult.getSuccessfulNodes()).thenReturn(3);
    when(mockResult.getFailedNodes()).thenReturn(0);
    when(mockResult.isAllSucceeded()).thenReturn(true);
    when(mockResult.getTotalExecutionTimeMs()).thenReturn(300L);
    when(mockResult.getNodeResults()).thenReturn(new LinkedHashMap<>());

    when(mockNodeScriptRunner.runScript(any(Universe.class), any(), any())).thenReturn(mockResult);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute
    RunScriptResponse response =
        handler.runScript(
            httpRequest, defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID(), request);

    // Verify
    assertNotNull(response);
    assertEquals(Boolean.TRUE, response.getSummary().getAllSucceeded());
  }

  @Test
  public void testRunScript_BothMastersAndTserversOnly_returnsBadRequest() {
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    scriptOptions.setScriptContent("echo 'test'");
    request.setScriptOptions(scriptOptions);

    NodeSelection nodeSelection = new NodeSelection();
    nodeSelection.setMastersOnly(true);
    nodeSelection.setTserversOnly(true);
    request.setNodes(nodeSelection);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.runScript(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    request));

    assertEquals(400, exception.getHttpStatus());
    assertTrue(
        exception.getMessage().contains("masters_only and tservers_only cannot both be true"));
    verify(mockNodeScriptRunner, never()).runScript(any(), any(), any());
  }

  @Test
  public void testRunScript_NoMatchingNodes() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    RunScriptRequest request = new RunScriptRequest();
    ScriptOptions scriptOptions = new ScriptOptions();
    scriptOptions.setScriptContent("echo 'test'");
    request.setScriptOptions(scriptOptions);

    NodeScriptRunner.ExecutionResult mockResult = mock(NodeScriptRunner.ExecutionResult.class);
    when(mockResult.getTotalNodes()).thenReturn(0);

    when(mockNodeScriptRunner.runScript(any(Universe.class), any(), any())).thenReturn(mockResult);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/run-script");

    // Execute & Verify
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.runScript(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    request));

    assertTrue(exception.getMessage().contains("No nodes found"));
  }

  // ==================== CREATE FILE COLLECTION TESTS ====================

  @Test
  public void testCreateFileCollection_Success() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    CollectFilesRequest request = new CollectFilesRequest();
    FileCollectionOptions options = new FileCollectionOptions();
    options.setFilePaths(Arrays.asList("/home/yugabyte/tserver/conf/server.conf"));
    options.setMaxFileSizeBytes(1048576L);
    request.setCollectionOptions(options);

    NodeFileCollector.CollectionResult mockResult = mock(NodeFileCollector.CollectionResult.class);
    when(mockResult.getCollectionUuid()).thenReturn(UUID.randomUUID());
    when(mockResult.isAllSucceeded()).thenReturn(true);
    when(mockResult.getTotalNodes()).thenReturn(3);
    when(mockResult.getSuccessfulNodes()).thenReturn(3);
    when(mockResult.getFailedNodes()).thenReturn(0);
    when(mockResult.getTotalFilesCollected()).thenReturn(3);
    when(mockResult.getTotalFilesSkipped()).thenReturn(0);
    when(mockResult.getTotalFilesFailed()).thenReturn(0);
    when(mockResult.getTotalBytesCollected()).thenReturn(10000L);
    when(mockResult.getTotalExecutionTimeMs()).thenReturn(500L);
    when(mockResult.getNodeResults()).thenReturn(new HashMap<>());

    when(mockNodeFileCollector.collectFiles(any(UUID.class), any(Universe.class), any(), any()))
        .thenReturn(mockResult);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections");

    // Execute
    CollectFilesResponse response =
        handler.createFileCollection(
            httpRequest, defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID(), request);

    // Verify
    assertNotNull(response);
    assertNotNull(response.getSummary());
    assertNotNull(response.getSummary().getCollectionUuid());
    assertEquals(Integer.valueOf(3), response.getSummary().getTotalNodes());
    assertEquals(Integer.valueOf(3), response.getSummary().getSuccessfulNodes());
    assertEquals(Boolean.TRUE, response.getSummary().getAllSucceeded());
  }

  @Test
  public void testCreateFileCollection_FeatureDisabled() {
    // Setup - feature is disabled
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(false);

    CollectFilesRequest request = new CollectFilesRequest();
    FileCollectionOptions options = new FileCollectionOptions();
    options.setFilePaths(Arrays.asList("/home/yugabyte/tserver/conf/server.conf"));
    request.setCollectionOptions(options);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections");

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.createFileCollection(
                httpRequest,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                request));
  }

  @Test
  public void testCreateFileCollection_LocalhostRequired() {
    // Setup - simulate localhost check failure
    doThrow(new PlatformServiceException(403, "Must be accessed from localhost"))
        .when(mockLocalhostChecker)
        .checkLocalhost(any(Request.class));

    CollectFilesRequest request = new CollectFilesRequest();
    FileCollectionOptions options = new FileCollectionOptions();
    options.setFilePaths(Arrays.asList("/home/yugabyte/tserver/conf/server.conf"));
    request.setCollectionOptions(options);

    Request httpRequest =
        createRemoteRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections");

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.createFileCollection(
                httpRequest,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                request));
  }

  @Test
  public void testCreateFileCollection_WithNodeSelection() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    CollectFilesRequest request = new CollectFilesRequest();
    FileCollectionOptions options = new FileCollectionOptions();
    options.setDirectoryPaths(Arrays.asList("/home/yugabyte/master/logs"));
    options.setMaxFileSizeBytes(102400L);
    options.setMaxDepth(2);
    request.setCollectionOptions(options);

    NodeSelection nodeSelection = new NodeSelection();
    nodeSelection.setMastersOnly(true);
    request.setNodes(nodeSelection);

    NodeFileCollector.CollectionResult mockResult = mock(NodeFileCollector.CollectionResult.class);
    when(mockResult.getCollectionUuid()).thenReturn(UUID.randomUUID());
    when(mockResult.isAllSucceeded()).thenReturn(true);
    when(mockResult.getTotalNodes()).thenReturn(3);
    when(mockResult.getSuccessfulNodes()).thenReturn(3);
    when(mockResult.getFailedNodes()).thenReturn(0);
    when(mockResult.getTotalFilesCollected()).thenReturn(6);
    when(mockResult.getTotalFilesSkipped()).thenReturn(3);
    when(mockResult.getTotalFilesFailed()).thenReturn(0);
    when(mockResult.getTotalBytesCollected()).thenReturn(50000L);
    when(mockResult.getTotalExecutionTimeMs()).thenReturn(800L);
    when(mockResult.getNodeResults()).thenReturn(new HashMap<>());

    when(mockNodeFileCollector.collectFiles(any(UUID.class), any(Universe.class), any(), any()))
        .thenReturn(mockResult);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections");

    // Execute
    CollectFilesResponse response =
        handler.createFileCollection(
            httpRequest, defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID(), request);

    // Verify
    assertNotNull(response);
    assertEquals(Integer.valueOf(6), response.getSummary().getTotalFilesCollected());
    assertEquals(Integer.valueOf(3), response.getSummary().getTotalFilesSkipped());
  }

  @Test
  public void testCreateFileCollection_BothMastersAndTserversOnly_returnsBadRequest() {
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    CollectFilesRequest request = new CollectFilesRequest();
    FileCollectionOptions options = new FileCollectionOptions();
    options.setFilePaths(Arrays.asList("/home/yugabyte/tserver/conf/server.conf"));
    request.setCollectionOptions(options);

    NodeSelection nodeSelection = new NodeSelection();
    nodeSelection.setMastersOnly(true);
    nodeSelection.setTserversOnly(true);
    request.setNodes(nodeSelection);

    Request httpRequest =
        createLocalhostRequest(
            "POST",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections");

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.createFileCollection(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    request));

    assertEquals(400, exception.getHttpStatus());
    assertTrue(
        exception.getMessage().contains("masters_only and tservers_only cannot both be true"));
    verify(mockNodeFileCollector, never()).collectFiles(any(), any(), any(), any());
  }

  // ==================== DOWNLOAD FILE COLLECTION TESTS ====================

  @Test
  public void testDownloadFileCollection_Success() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    byte[] mockArchiveContent = "mock tar.gz content".getBytes();
    InputStream mockInputStream = new ByteArrayInputStream(mockArchiveContent);

    when(mockFileCollectionDownloader.downloadAsStream(eq(collectionUuid), any(Universe.class)))
        .thenReturn(mockInputStream);

    // Execute - cleanupDbNodesAfter=false doesn't require localhost
    Request request =
        createRemoteRequest("GET", "/file-collections/" + collectionUuid + "/download");
    InputStream result =
        handler.downloadFileCollection(
            request,
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            collectionUuid,
            false);

    // Verify
    assertNotNull(result);
    verify(mockFileCollectionDownloader).downloadAsStream(eq(collectionUuid), any(Universe.class));
    // cleanupCollection should NOT be called when cleanupDbNodesAfter is false
    verify(mockFileCollectionDownloader, never())
        .cleanupCollection(any(), any(), anyBoolean(), anyBoolean());
  }

  @Test
  public void testDownloadFileCollection_WithCleanupAfter() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    byte[] mockArchiveContent = "mock tar.gz content".getBytes();
    InputStream mockInputStream = new ByteArrayInputStream(mockArchiveContent);

    when(mockFileCollectionDownloader.downloadAsStream(eq(collectionUuid), any(Universe.class)))
        .thenReturn(mockInputStream);
    when(mockFileCollectionDownloader.cleanupCollection(
            eq(collectionUuid), any(Universe.class), eq(true), eq(false)))
        .thenReturn(3);

    // Execute with cleanupDbNodesAfter = true (requires localhost)
    Request request =
        createLocalhostRequest("GET", "/file-collections/" + collectionUuid + "/download");
    InputStream result =
        handler.downloadFileCollection(
            request,
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            collectionUuid,
            true);

    // Verify
    assertNotNull(result);
    verify(mockLocalhostChecker).checkLocalhost(request);
    verify(mockFileCollectionDownloader).downloadAsStream(eq(collectionUuid), any(Universe.class));
    // cleanupCollection SHOULD be called when cleanupDbNodesAfter is true
    verify(mockFileCollectionDownloader)
        .cleanupCollection(eq(collectionUuid), any(Universe.class), eq(true), eq(false));
  }

  @Test
  public void testDownloadFileCollection_FeatureDisabled() {
    // Setup - feature is disabled
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(false);

    UUID collectionUuid = UUID.randomUUID();
    Request request =
        createRemoteRequest("GET", "/file-collections/" + collectionUuid + "/download");

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.downloadFileCollection(
                request,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                collectionUuid,
                false));

    verify(mockFileCollectionDownloader, never()).downloadAsStream(any(), any());
  }

  @Test
  public void testDownloadFileCollection_CollectionNotFound() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    when(mockFileCollectionDownloader.downloadAsStream(eq(collectionUuid), any(Universe.class)))
        .thenThrow(new PlatformServiceException(400, "Collection not found"));
    Request request =
        createRemoteRequest("GET", "/file-collections/" + collectionUuid + "/download");

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.downloadFileCollection(
                request,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                collectionUuid,
                false));
  }

  @Test
  public void testDownloadFileCollection_AlreadyCleanedUp() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    when(mockFileCollectionDownloader.downloadAsStream(eq(collectionUuid), any(Universe.class)))
        .thenThrow(
            new PlatformServiceException(400, "Collection files have been cleaned from DB nodes"));
    Request request =
        createRemoteRequest("GET", "/file-collections/" + collectionUuid + "/download");

    // Execute & Verify
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.downloadFileCollection(
                    request,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    collectionUuid,
                    false));

    assertEquals(400, exception.getHttpStatus());
  }

  @Test
  public void testDownloadFileCollection_WithCleanupAfter_RejectsRemoteRequest() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    Request request =
        createRemoteRequest("GET", "/file-collections/" + collectionUuid + "/download");

    // Configure localhost checker to throw when remote request with cleanup
    doThrow(new PlatformServiceException(403, "This API is only accessible from localhost"))
        .when(mockLocalhostChecker)
        .checkLocalhost(request);

    // Execute & Verify - should fail because cleanup requires localhost
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.downloadFileCollection(
                    request,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    collectionUuid,
                    true)); // cleanupDbNodesAfter = true requires localhost

    assertEquals(403, exception.getHttpStatus());
    verify(mockLocalhostChecker).checkLocalhost(request);
    // Download should NOT happen because localhost check failed first
    verify(mockFileCollectionDownloader, never()).downloadAsStream(any(), any());
  }

  // ==================== DELETE FILE COLLECTION TESTS ====================

  @Test
  public void testDeleteFileCollection_Success_DbNodesOnly() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    when(mockFileCollectionDownloader.cleanupCollection(
            eq(collectionUuid), any(Universe.class), eq(true), eq(false)))
        .thenReturn(3);

    Request httpRequest =
        createLocalhostRequest(
            "DELETE",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections/"
                + collectionUuid);

    // Execute
    int nodesCleaned =
        handler.deleteFileCollection(
            httpRequest,
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            collectionUuid,
            true, // deleteFromDbNodes
            false); // deleteFromYba

    // Verify
    assertEquals(3, nodesCleaned);
    verify(mockFileCollectionDownloader)
        .cleanupCollection(eq(collectionUuid), any(Universe.class), eq(true), eq(false));
  }

  @Test
  public void testDeleteFileCollection_Success_Both() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    when(mockFileCollectionDownloader.cleanupCollection(
            eq(collectionUuid), any(Universe.class), eq(true), eq(true)))
        .thenReturn(3);

    Request httpRequest =
        createLocalhostRequest(
            "DELETE",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections/"
                + collectionUuid);

    // Execute
    int nodesCleaned =
        handler.deleteFileCollection(
            httpRequest,
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            collectionUuid,
            true, // deleteFromDbNodes
            true); // deleteFromYba

    // Verify
    assertEquals(3, nodesCleaned);
    verify(mockFileCollectionDownloader)
        .cleanupCollection(eq(collectionUuid), any(Universe.class), eq(true), eq(true));
  }

  @Test
  public void testDeleteFileCollection_Success_YbaOnly() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    when(mockFileCollectionDownloader.cleanupCollection(
            eq(collectionUuid), any(Universe.class), eq(false), eq(true)))
        .thenReturn(0);

    Request httpRequest =
        createLocalhostRequest(
            "DELETE",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections/"
                + collectionUuid);

    // Execute
    int nodesCleaned =
        handler.deleteFileCollection(
            httpRequest,
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            collectionUuid,
            false, // deleteFromDbNodes
            true); // deleteFromYba

    // Verify
    assertEquals(0, nodesCleaned);
    verify(mockFileCollectionDownloader)
        .cleanupCollection(eq(collectionUuid), any(Universe.class), eq(false), eq(true));
  }

  @Test
  public void testDeleteFileCollection_FeatureDisabled() {
    // Setup - feature is disabled
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(false);

    UUID collectionUuid = UUID.randomUUID();

    Request httpRequest =
        createLocalhostRequest(
            "DELETE",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections/"
                + collectionUuid);

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.deleteFileCollection(
                httpRequest,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                collectionUuid,
                true,
                false));

    verify(mockFileCollectionDownloader, never())
        .cleanupCollection(any(), any(), anyBoolean(), anyBoolean());
  }

  @Test
  public void testDeleteFileCollection_LocalhostRequired() {
    // Setup - simulate localhost check failure
    doThrow(new PlatformServiceException(403, "Must be accessed from localhost"))
        .when(mockLocalhostChecker)
        .checkLocalhost(any(Request.class));

    UUID collectionUuid = UUID.randomUUID();

    Request httpRequest =
        createRemoteRequest(
            "DELETE",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections/"
                + collectionUuid);

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.deleteFileCollection(
                httpRequest,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                collectionUuid,
                true,
                false));
  }

  @Test
  public void testDeleteFileCollection_CollectionNotFound() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    when(mockFileCollectionDownloader.cleanupCollection(
            eq(collectionUuid), any(Universe.class), eq(true), eq(false)))
        .thenThrow(new PlatformServiceException(400, "Collection not found"));

    Request httpRequest =
        createLocalhostRequest(
            "DELETE",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections/"
                + collectionUuid);

    // Execute & Verify
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.deleteFileCollection(
                httpRequest,
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                collectionUuid,
                true,
                false));
  }

  @Test
  public void testDeleteFileCollection_AlreadyCleanedUp() {
    // Setup
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.nodeScriptEnabled)))
        .thenReturn(true);

    UUID collectionUuid = UUID.randomUUID();
    when(mockFileCollectionDownloader.cleanupCollection(
            eq(collectionUuid), any(Universe.class), eq(true), eq(false)))
        .thenThrow(
            new PlatformServiceException(400, "Collection has already been fully cleaned up"));

    Request httpRequest =
        createLocalhostRequest(
            "DELETE",
            "/api/v2/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/file-collections/"
                + collectionUuid);

    // Execute & Verify
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.deleteFileCollection(
                    httpRequest,
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    collectionUuid,
                    true,
                    false));

    assertEquals(400, exception.getHttpStatus());
  }
}
