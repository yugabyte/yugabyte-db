// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.YBCertificate;
import io.yugabyte.operator.v1alpha1.YBCertificateSpec;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YBCertificateReconcilerTest extends FakeDBApplication {

  @Mock SharedIndexInformer<YBCertificate> certificateInformer;

  @Mock
  MixedOperation<YBCertificate, KubernetesResourceList<YBCertificate>, Resource<YBCertificate>>
      resourceClient;

  @Mock OperatorUtils operatorUtils;
  @Mock RuntimeConfGetter runtimeConfGetter;
  @Mock Indexer<YBCertificate> indexer;

  @Mock
  NonNamespaceOperation<
          YBCertificate, KubernetesResourceList<YBCertificate>, Resource<YBCertificate>>
      inNamespaceResource;

  @Mock Resource<YBCertificate> certificateResource;

  private YBCertificateReconciler ybCertificateReconciler;
  private static final String NAMESPACE = "test-namespace";

  @Before
  public void setup() throws Exception {
    when(certificateInformer.getIndexer()).thenReturn(indexer);
    when(resourceClient.inNamespace(anyString())).thenReturn(inNamespaceResource);
    when(inNamespaceResource.resource(any(YBCertificate.class))).thenReturn(certificateResource);
    when(operatorUtils.getCustomerUUID()).thenThrow(new RuntimeException("mock - skip processing"));
    ybCertificateReconciler =
        new YBCertificateReconciler(
            certificateInformer, resourceClient, NAMESPACE, operatorUtils, runtimeConfGetter);
  }

  private YBCertificate createYBCertificateCr(String name) {
    YBCertificate certificate = new YBCertificate();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    certificate.setMetadata(metadata);
    certificate.setStatus(null);
    YBCertificateSpec spec = new YBCertificateSpec();
    spec.setCertType(YBCertificateSpec.CertType.SELF_SIGNED);
    certificate.setSpec(spec);
    return certificate;
  }

  @Test
  public void testOnAddAddsResourceToTrackedResources() {
    String certName = "test-ybcertificate";
    YBCertificate certificate = createYBCertificateCr(certName);

    ybCertificateReconciler.onAdd(certificate);

    assertEquals(1, ybCertificateReconciler.getTrackedResources().size());
    KubernetesResourceDetails details =
        ybCertificateReconciler.getTrackedResources().iterator().next();
    assertEquals(certName, details.name);
    assertEquals(NAMESPACE, details.namespace);

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the certificate name",
        allResources.get(0).getName().contains(certName));
    YBCertificate rCert =
        Serialization.unmarshal(allResources.get(0).getData(), YBCertificate.class);
    assertEquals(certName, rCert.getMetadata().getName());
    assertEquals(NAMESPACE, rCert.getMetadata().getNamespace());
    assertEquals(YBCertificateSpec.CertType.SELF_SIGNED, rCert.getSpec().getCertType());
  }

  @Test
  public void testOnDeleteRemovesOperatorResource() {
    String certName = "test-ybcertificate-delete";
    YBCertificate certificate = createYBCertificateCr(certName);

    ybCertificateReconciler.onAdd(certificate);
    assertEquals(1, OperatorResource.getAll().size());

    // Delete - processCertificateDeletion will fail (getCustomerUUID throws),
    // but untrackResource runs first
    ybCertificateReconciler.onDelete(certificate, false);

    assertTrue(
        "Tracked resources should be empty after delete",
        ybCertificateReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "OperatorResource entries should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }
}
