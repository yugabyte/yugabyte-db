// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.AppsAPIGroupDSL;
import io.fabric8.kubernetes.client.dsl.FilterWatchListDeletable;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;
import org.mockito.InOrder;

public class NativeKubernetesManagerTest {

  @Test
  @SuppressWarnings({"rawtypes", "unchecked"})
  public void testDeleteStorageDeletesStatefulSetsThenPVCs() {
    NativeKubernetesManager kubernetesManager = spy(new NativeKubernetesManager());
    KubernetesClient client = mock(KubernetesClient.class);
    doReturn(client).when(kubernetesManager).getClient(any());

    AppsAPIGroupDSL apps = mock(AppsAPIGroupDSL.class);
    MixedOperation statefulSetOperation = mock(MixedOperation.class);
    NonNamespaceOperation statefulSetsInNamespace = mock(NonNamespaceOperation.class);
    FilterWatchListDeletable statefulSets = mock(FilterWatchListDeletable.class);
    when(client.apps()).thenReturn(apps);
    when(apps.statefulSets()).thenReturn(statefulSetOperation);
    when(statefulSetOperation.inNamespace("demo-namespace")).thenReturn(statefulSetsInNamespace);
    when(statefulSetsInNamespace.withLabelIn("release", "demo-az1", "demo-az2"))
        .thenReturn(statefulSets);

    MixedOperation pvcOperation = mock(MixedOperation.class);
    NonNamespaceOperation pvcsInNamespace = mock(NonNamespaceOperation.class);
    FilterWatchListDeletable pvcs = mock(FilterWatchListDeletable.class);
    when(client.persistentVolumeClaims()).thenReturn(pvcOperation);
    when(pvcOperation.inNamespace("demo-namespace")).thenReturn(pvcsInNamespace);
    when(pvcsInNamespace.withLabelIn("release", "demo-az1", "demo-az2")).thenReturn(pvcs);

    kubernetesManager.deleteStorage(
        UUID.randomUUID(),
        Map.of("KUBECONFIG", "test"),
        ImmutableList.of("demo-az1", "demo-az2"),
        "demo-namespace");

    InOrder inOrder = inOrder(statefulSets, pvcs);
    inOrder.verify(statefulSets).delete();
    inOrder.verify(pvcs).delete();
  }
}
