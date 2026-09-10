// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anySet;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.same;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.common.operator.utils.TelemetryProviderCrConverter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.TelemetryProvider;
import io.yugabyte.operator.v1alpha1.TelemetryProviderSpec;
import io.yugabyte.operator.v1alpha1.TelemetryProviderStatus;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.DataDog;
import io.yugabyte.operator.v1alpha1.telemetryproviderspec.datadog.ApiKeySecret;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

/**
 * Tests for {@link TelemetryProviderReconciler}.
 *
 * <p>Note the name collision: {@code TelemetryProvider} here is always the custom resource ({@code
 * io.yugabyte.operator.v1alpha1.TelemetryProvider}); the YBA model is always spelled out as {@code
 * com.yugabyte.yw.models.TelemetryProvider}.
 */
@RunWith(MockitoJUnitRunner.class)
public class TelemetryProviderReconcilerTest extends FakeDBApplication {

  private static final String NAMESPACE = "test-namespace";
  // Deliberately different from the Kubernetes object names used below: the YBA provider name must
  // always come from spec.name.
  private static final String SPEC_NAME = "yba-telemetry-provider";
  private static final String CR_NAME = "tp-cr";

  @Mock KubernetesClient client;
  @Mock YBInformerFactory informerFactory;
  @Mock SharedIndexInformer<TelemetryProvider> informer;

  @Mock
  MixedOperation<
          TelemetryProvider, KubernetesResourceList<TelemetryProvider>, Resource<TelemetryProvider>>
      resourceClient;

  @Mock
  NonNamespaceOperation<
          TelemetryProvider, KubernetesResourceList<TelemetryProvider>, Resource<TelemetryProvider>>
      inNamespaceResource;

  @Mock Resource<TelemetryProvider> providerResource;

  @Mock OperatorUtils operatorUtils;
  @Mock TelemetryProviderService telemetryProviderService;
  @Mock TelemetryProviderCrConverter crConverter;

  private Customer customer;
  private DataDogConfig convertedConfig;
  private TelemetryProviderReconciler reconciler;
  private MockedStatic<OperatorUtils> operatorUtilsStatic;

  @Before
  public void setup() throws Exception {
    customer = ModelFactory.testCustomer();

    lenient()
        .when(informerFactory.getSharedIndexInformer(eq(TelemetryProvider.class), any()))
        .thenReturn(informer);
    lenient().when(client.resources(eq(TelemetryProvider.class))).thenReturn(resourceClient);
    lenient().when(resourceClient.inNamespace(anyString())).thenReturn(inNamespaceResource);
    lenient()
        .when(inNamespaceResource.resource(any(TelemetryProvider.class)))
        .thenReturn(providerResource);
    lenient().when(inNamespaceResource.withName(anyString())).thenReturn(providerResource);

    lenient().when(operatorUtils.getOperatorCustomer()).thenReturn(customer);
    lenient().when(operatorUtils.getLocalPlatformInstanceUuid()).thenReturn(Optional.empty());

    convertedConfig = new DataDogConfig();
    convertedConfig.setSite("datadoghq.com");
    convertedConfig.setApiKey("api-key-from-secret");
    lenient().when(crConverter.toConfig(any(), any(), any(), any())).thenReturn(convertedConfig);

    reconciler =
        new TelemetryProviderReconciler(
            telemetryProviderService,
            crConverter,
            NAMESPACE,
            operatorUtils,
            client,
            informerFactory);

    // maybeAddYbaResourceId is a static helper on OperatorUtils.
    operatorUtilsStatic = Mockito.mockStatic(OperatorUtils.class);
  }

  @After
  public void tearDown() {
    if (operatorUtilsStatic != null) {
      operatorUtilsStatic.close();
    }
  }

  // ==================== Fixtures ====================

  private TelemetryProvider createCr(String metadataName, String specName) {
    TelemetryProvider cr = new TelemetryProvider();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(metadataName);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    metadata.setGeneration(1L);
    cr.setMetadata(metadata);

    TelemetryProviderSpec spec = new TelemetryProviderSpec();
    spec.setProvider(TelemetryProviderSpec.Provider.DATA_DOG);
    DataDog dataDog = new DataDog();
    dataDog.setSite("datadoghq.com");
    ApiKeySecret apiKeySecret = new ApiKeySecret();
    apiKeySecret.setName("datadog-secret");
    apiKeySecret.setKey("apiKey");
    dataDog.setApiKeySecret(apiKeySecret);
    spec.setDataDog(dataDog);
    Map<String, String> tags = new LinkedHashMap<>();
    tags.put("env", "dev");
    tags.put("team", "platform");
    spec.setTags(tags);
    cr.setSpec(spec);
    return cr;
  }

  private TelemetryProvider createCr() {
    return createCr(CR_NAME, SPEC_NAME);
  }

  private com.yugabyte.yw.models.TelemetryProvider ybaProvider(UUID uuid, String name) {
    com.yugabyte.yw.models.TelemetryProvider provider =
        new com.yugabyte.yw.models.TelemetryProvider();
    provider.setUuid(uuid);
    provider.setCustomerUUID(customer.getUuid());
    provider.setName(name);
    provider.setConfig(convertedConfig);
    provider.setTags(new HashMap<>());
    return provider;
  }

  /** Stubs save() to assign the given UUID to whatever provider is handed to it. */
  private void stubSaveAssigning(UUID uuid) {
    when(telemetryProviderService.save(any(com.yugabyte.yw.models.TelemetryProvider.class)))
        .thenAnswer(
            invocation -> {
              com.yugabyte.yw.models.TelemetryProvider provider = invocation.getArgument(0);
              provider.setUuid(uuid);
              return provider;
            });
  }

  private void setStatusResourceUUID(TelemetryProvider cr, UUID uuid) {
    TelemetryProviderStatus status = new TelemetryProviderStatus();
    status.setState("Ready");
    status.setResourceUUID(uuid.toString());
    cr.setStatus(status);
  }

  private ArgumentCaptor<com.yugabyte.yw.models.TelemetryProvider> saveCaptor() {
    return ArgumentCaptor.forClass(com.yugabyte.yw.models.TelemetryProvider.class);
  }

  private UUID verifyResourceIdAnnotated() {
    ArgumentCaptor<UUID> uuidCaptor = ArgumentCaptor.forClass(UUID.class);
    operatorUtilsStatic.verify(
        () ->
            OperatorUtils.<TelemetryProvider>maybeAddYbaResourceId(
                any(), uuidCaptor.capture(), any()));
    return uuidCaptor.getValue();
  }

  // ==================== CREATE ====================

  @Test
  public void testCreateActionCreatesProviderInYba() throws Exception {
    TelemetryProvider cr = createCr();
    UUID createdUuid = UUID.randomUUID();
    stubSaveAssigning(createdUuid);

    reconciler.createActionReconcile(cr, customer);

    ArgumentCaptor<com.yugabyte.yw.models.TelemetryProvider> captor = saveCaptor();
    verify(telemetryProviderService, times(1)).save(captor.capture());
    com.yugabyte.yw.models.TelemetryProvider saved = captor.getValue();
    assertEquals(customer.getUuid(), saved.getCustomerUUID());
    assertEquals(CR_NAME, saved.getName());
    assertSame(convertedConfig, saved.getConfig());
    Map<String, String> expectedTags = new HashMap<>();
    expectedTags.put("env", "dev");
    expectedTags.put("team", "platform");
    assertEquals(expectedTags, saved.getTags());
    // Tags must be copied, not aliased to the CR spec map.
    assertNotSame(cr.getSpec().getTags(), saved.getTags());

    assertEquals(createdUuid, verifyResourceIdAnnotated());

    assertNotNull(cr.getStatus());
    assertEquals("Ready", cr.getStatus().getState());
    assertEquals(createdUuid.toString(), cr.getStatus().getResourceUUID());
  }

  @Test
  public void testCreateActionAddsFinalizer() throws Exception {
    TelemetryProvider cr = createCr();
    stubSaveAssigning(UUID.randomUUID());

    reconciler.createActionReconcile(cr, customer);

    verify(providerResource, times(1)).patch(same(cr));
    assertEquals(OperatorUtils.YB_FINALIZER, cr.getMetadata().getFinalizers().get(0));
  }

  @Test
  public void testCreateActionAdoptsExistingProvider() throws Exception {
    TelemetryProvider cr = createCr();
    UUID existingUuid = UUID.randomUUID();
    when(telemetryProviderService.list(any(UUID.class), anySet()))
        .thenReturn(Collections.singletonList(ybaProvider(existingUuid, SPEC_NAME)));

    reconciler.createActionReconcile(cr, customer);

    verify(telemetryProviderService, never())
        .save(any(com.yugabyte.yw.models.TelemetryProvider.class));
    assertEquals(existingUuid, verifyResourceIdAnnotated());
    assertNotNull(cr.getStatus());
    assertEquals("Ready", cr.getStatus().getState());
    assertEquals(existingUuid.toString(), cr.getStatus().getResourceUUID());
  }

  @Test
  public void testCreateActionTwiceSavesOnlyOnce() throws Exception {
    TelemetryProvider cr = createCr();
    UUID createdUuid = UUID.randomUUID();
    stubSaveAssigning(createdUuid);
    // First lookup finds nothing, the second one finds what the first pass created.
    when(telemetryProviderService.list(any(UUID.class), anySet()))
        .thenReturn(Collections.emptyList())
        .thenReturn(Collections.singletonList(ybaProvider(createdUuid, SPEC_NAME)));

    reconciler.createActionReconcile(cr, customer);
    reconciler.createActionReconcile(cr, customer);

    verify(telemetryProviderService, times(1))
        .save(any(com.yugabyte.yw.models.TelemetryProvider.class));
    verify(telemetryProviderService, times(2)).list(any(UUID.class), anySet());
    assertEquals("Ready", cr.getStatus().getState());
    assertEquals(createdUuid.toString(), cr.getStatus().getResourceUUID());
  }

  @Test
  public void testLookupAndCreateBothUseSpecName() throws Exception {
    String metadataName = "metadata-name-differs";
    TelemetryProvider cr = createCr(metadataName, SPEC_NAME);
    stubSaveAssigning(UUID.randomUUID());

    reconciler.createActionReconcile(cr, customer);

    @SuppressWarnings("unchecked")
    ArgumentCaptor<Set<String>> namesCaptor = ArgumentCaptor.forClass(Set.class);
    ArgumentCaptor<UUID> customerCaptor = ArgumentCaptor.forClass(UUID.class);
    verify(telemetryProviderService, times(1))
        .list(customerCaptor.capture(), namesCaptor.capture());
    assertEquals(customer.getUuid(), customerCaptor.getValue());
    assertEquals(Collections.singleton(metadataName), namesCaptor.getValue());

    ArgumentCaptor<com.yugabyte.yw.models.TelemetryProvider> captor = saveCaptor();
    verify(telemetryProviderService, times(1)).save(captor.capture());
    String createdName = captor.getValue().getName();

    assertEquals(metadataName, createdName);
    assertEquals(namesCaptor.getValue().iterator().next(), createdName);
  }

  @Test
  public void testCreateActionFailureSetsErrorStatus() throws Exception {
    TelemetryProvider cr = createCr();
    doThrow(new RuntimeException("datadog api key is invalid"))
        .when(telemetryProviderService)
        .validateTelemetryProvider(any(com.yugabyte.yw.models.TelemetryProvider.class));

    // Must not propagate: the reconcile loop can only log an exception, the CR status is what the
    // user sees.
    reconciler.createActionReconcile(cr, customer);

    verify(telemetryProviderService, never())
        .save(any(com.yugabyte.yw.models.TelemetryProvider.class));
    operatorUtilsStatic.verify(
        () -> OperatorUtils.<TelemetryProvider>maybeAddYbaResourceId(any(), any(), any()), never());
    assertNotNull(cr.getStatus());
    assertEquals("Error", cr.getStatus().getState());
    String message = cr.getStatus().getMessage();
    assertTrue(message, message.contains("datadog api key is invalid"));
  }

  // ==================== UPDATE / NO_OP ====================

  @Test
  public void testUpdateActionCreatesProviderWhenMissing() throws Exception {
    TelemetryProvider cr = createCr();
    UUID createdUuid = UUID.randomUUID();
    stubSaveAssigning(createdUuid);
    // list() is unstubbed and returns an empty list: the provider does not exist in YBA yet.

    reconciler.updateActionReconcile(cr, customer);

    ArgumentCaptor<com.yugabyte.yw.models.TelemetryProvider> captor = saveCaptor();
    verify(telemetryProviderService, times(1)).save(captor.capture());
    assertEquals(CR_NAME, captor.getValue().getName());
    assertEquals("Ready", cr.getStatus().getState());
    assertEquals(createdUuid.toString(), cr.getStatus().getResourceUUID());
  }

  @Test
  public void testUpdateActionDoesNotPushConfigChangesToExistingProvider() throws Exception {
    TelemetryProvider cr = createCr();
    cr.getSpec().getDataDog().setSite("us5.datadoghq.com");
    UUID existingUuid = UUID.randomUUID();
    when(telemetryProviderService.list(any(UUID.class), anySet()))
        .thenReturn(Collections.singletonList(ybaProvider(existingUuid, SPEC_NAME)));

    reconciler.updateActionReconcile(cr, customer);

    // YBA has no telemetry provider edit API, so an existing provider is left untouched.
    verify(telemetryProviderService, never())
        .save(any(com.yugabyte.yw.models.TelemetryProvider.class));
    verify(telemetryProviderService, never()).delete(any(UUID.class));
    assertEquals("Ready", cr.getStatus().getState());
    assertEquals(existingUuid.toString(), cr.getStatus().getResourceUUID());
  }

  @Test
  public void testNoOpActionReportsReadyForExistingProvider() throws Exception {
    TelemetryProvider cr = createCr();
    UUID existingUuid = UUID.randomUUID();
    when(telemetryProviderService.list(any(UUID.class), anySet()))
        .thenReturn(Collections.singletonList(ybaProvider(existingUuid, SPEC_NAME)));

    reconciler.noOpActionReconcile(cr, customer);

    verify(telemetryProviderService, never())
        .save(any(com.yugabyte.yw.models.TelemetryProvider.class));
    assertEquals(existingUuid, verifyResourceIdAnnotated());
    assertEquals("Ready", cr.getStatus().getState());
    assertEquals(existingUuid.toString(), cr.getStatus().getResourceUUID());
  }

  @Test
  public void testNoOpActionDoesNotCreateWhenProviderMissing() throws Exception {
    TelemetryProvider cr = createCr();
    // list() is unstubbed and returns an empty list: a CREATE is requeued instead of creating the
    // provider from the no-op pass.

    reconciler.noOpActionReconcile(cr, customer);

    verify(telemetryProviderService, never())
        .save(any(com.yugabyte.yw.models.TelemetryProvider.class));
    operatorUtilsStatic.verify(
        () -> OperatorUtils.<TelemetryProvider>maybeAddYbaResourceId(any(), any(), any()), never());
  }

  // ==================== DELETE ====================

  @Test
  public void testReconcileWithDeletionTimestampRunsDeletePath() throws Exception {
    TelemetryProvider cr = createCr();
    UUID providerUuid = UUID.randomUUID();
    setStatusResourceUUID(cr, providerUuid);
    cr.getMetadata().setDeletionTimestamp("2026-01-01T00:00:00Z");
    when(telemetryProviderService.get(providerUuid))
        .thenReturn(ybaProvider(providerUuid, SPEC_NAME));

    // A pending deletion must take the delete path whatever action the work queue carried.
    reconciler.reconcile(cr, ResourceAction.NO_OP);

    verify(telemetryProviderService, never())
        .save(any(com.yugabyte.yw.models.TelemetryProvider.class));
    verify(telemetryProviderService, times(1)).delete(providerUuid);
    verify(operatorUtils, times(1)).removeFinalizer(same(cr), same(resourceClient));
  }

  @Test
  public void testDeleteInUseDoesNotDeleteAndReportsInUse() throws Exception {
    TelemetryProvider cr = createCr();
    UUID providerUuid = UUID.randomUUID();
    setStatusResourceUUID(cr, providerUuid);
    when(telemetryProviderService.get(providerUuid))
        .thenReturn(ybaProvider(providerUuid, SPEC_NAME));
    when(telemetryProviderService.isProviderInUse(customer, providerUuid)).thenReturn(true);

    reconciler.handleResourceDeletion(cr, customer, ResourceAction.DELETE);

    verify(telemetryProviderService, never()).delete(any(UUID.class));
    // The finalizer must be kept so the requeued deletion can retry.
    verify(operatorUtils, never()).removeFinalizer(any(TelemetryProvider.class), any());
    assertNotNull(cr.getStatus());
    assertEquals("InUse", cr.getStatus().getState());
    assertEquals(providerUuid.toString(), cr.getStatus().getResourceUUID());
    String message = cr.getStatus().getMessage();
    assertTrue(message, message.contains(SPEC_NAME));
    assertTrue(message, message.contains(providerUuid.toString()));
  }

  @Test
  public void testDeleteNotInUseDeletesProvider() throws Exception {
    TelemetryProvider cr = createCr();
    UUID providerUuid = UUID.randomUUID();
    setStatusResourceUUID(cr, providerUuid);
    when(telemetryProviderService.get(providerUuid))
        .thenReturn(ybaProvider(providerUuid, SPEC_NAME));
    // isProviderInUse is unstubbed and returns false.

    reconciler.handleResourceDeletion(cr, customer, ResourceAction.DELETE);

    ArgumentCaptor<UUID> deletedUuid = ArgumentCaptor.forClass(UUID.class);
    verify(telemetryProviderService, times(1)).delete(deletedUuid.capture());
    assertEquals(providerUuid, deletedUuid.getValue());
    verify(operatorUtils, times(1)).removeFinalizer(same(cr), same(resourceClient));
  }

  @Test
  public void testDeleteResolvesProviderByStatusResourceUUID() throws Exception {
    TelemetryProvider cr = createCr();
    UUID providerUuid = UUID.randomUUID();
    setStatusResourceUUID(cr, providerUuid);
    when(telemetryProviderService.get(providerUuid))
        .thenReturn(ybaProvider(providerUuid, SPEC_NAME));

    reconciler.handleResourceDeletion(cr, customer, ResourceAction.DELETE);

    verify(telemetryProviderService, times(1)).get(providerUuid);
    // The status UUID is authoritative, no name lookup is needed.
    verify(telemetryProviderService, never()).list(any(UUID.class), anySet());
    verify(telemetryProviderService, times(1)).delete(providerUuid);
  }

  @Test
  public void testDeleteFallsBackToNameLookupWithoutStatusResourceUUID() throws Exception {
    TelemetryProvider cr = createCr();
    // No status at all: the provider can only be resolved by its spec.name.
    UUID providerUuid = UUID.randomUUID();
    when(telemetryProviderService.list(any(UUID.class), anySet()))
        .thenReturn(Collections.singletonList(ybaProvider(providerUuid, SPEC_NAME)));

    reconciler.handleResourceDeletion(cr, customer, ResourceAction.DELETE);

    verify(telemetryProviderService, never()).get(any(UUID.class));
    @SuppressWarnings("unchecked")
    ArgumentCaptor<Set<String>> namesCaptor = ArgumentCaptor.forClass(Set.class);
    ArgumentCaptor<UUID> customerCaptor = ArgumentCaptor.forClass(UUID.class);
    verify(telemetryProviderService, times(1))
        .list(customerCaptor.capture(), namesCaptor.capture());
    assertEquals(customer.getUuid(), customerCaptor.getValue());
    assertEquals(Collections.singleton(CR_NAME), namesCaptor.getValue());
    verify(telemetryProviderService, times(1)).delete(providerUuid);
  }

  @Test
  public void testDeleteMissingProviderStillReleasesResource() throws Exception {
    TelemetryProvider cr = createCr();
    // Neither the status UUID nor the name resolves to anything: nothing to delete in YBA, but the
    // object must still be released.
    List<com.yugabyte.yw.models.TelemetryProvider> empty = Collections.emptyList();
    when(telemetryProviderService.list(any(UUID.class), anySet())).thenReturn(empty);

    reconciler.handleResourceDeletion(cr, customer, ResourceAction.DELETE);

    verify(telemetryProviderService, never()).delete(any(UUID.class));
    verify(operatorUtils, times(1)).removeFinalizer(same(cr), same(resourceClient));
  }
}
