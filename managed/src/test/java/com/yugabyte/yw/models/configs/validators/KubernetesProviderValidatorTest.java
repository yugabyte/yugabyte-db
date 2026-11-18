// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

public class KubernetesProviderValidatorTest extends FakeDBApplication {

  private KubernetesManager kubernetesManager;
  private KubernetesProviderValidator validator;
  private Provider provider;

  @Before
  public void setUp() {
    RuntimeConfGetter confGetter = mock(RuntimeConfGetter.class);
    when(confGetter.getGlobalConf(GlobalConfKeys.enableK8sProviderValidation)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath))
        .thenReturn(System.getProperty("java.io.tmpdir"));
    kubernetesManager = mock(KubernetesManager.class);
    KubernetesManagerFactory kubernetesManagerFactory = mock(KubernetesManagerFactory.class);
    when(kubernetesManagerFactory.getManager()).thenReturn(kubernetesManager);
    validator =
        new KubernetesProviderValidator(
            app.injector().instanceOf(BeanValidator.class), confGetter, kubernetesManagerFactory);
    provider = ModelFactory.kubernetesProvider(ModelFactory.testCustomer());
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testNamespaceCheckGetsTheOverridesCommonLabels() {
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    CloudInfoInterface.setCloudProviderInfoFromConfig(
        region,
        new HashMap<>(
            Map.of("KUBENAMESPACE", "demo-ns", "OVERRIDES", "commonLabels:\n  team: db\n")));
    region.save();
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    CloudInfoInterface.setCloudProviderInfoFromConfig(
        az1, new HashMap<>(Map.of("OVERRIDES", "commonLabels:\n  team: zone\n")));
    az1.save();
    AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(region, "az-3", "AZ 3", "subnet-3");
    CloudInfoInterface.setCloudProviderInfoFromConfig(
        az3, new HashMap<>(Map.of("OVERRIDES", "commonLabels: [")));
    az3.save();
    when(kubernetesManager.dbNamespaceExists(any(), eq("demo-ns"), any())).thenReturn(true);

    // The cluster has no nodes, so the zone label check fails the validation.
    assertThrows(
        PlatformServiceException.class,
        () -> validator.validate(Provider.getOrBadRequest(provider.getUuid())));

    ArgumentCaptor<Map<String, String>> labels = ArgumentCaptor.forClass(Map.class);
    verify(kubernetesManager, times(3)).dbNamespaceExists(any(), eq("demo-ns"), labels.capture());
    // A zone's overrides replace the region's, as they do for helm; invalid ones add no labels.
    assertEquals(
        Set.of(Map.of("team", "zone"), Map.of("team", "db"), Map.of()),
        new HashSet<>(labels.getAllValues()));
  }
}
