// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import java.util.UUID;
import org.junit.Test;

public class ImageBundleTest extends FakeDBApplication {

  @Test
  public void testGetUniverseCount() {
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    ImageBundle defaultBundle =
        ImageBundle.create(provider, "default-bundle", new ImageBundleDetails(), true);
    ImageBundle customBundle =
        ImageBundle.create(provider, "custom-bundle", new ImageBundleDetails(), false);

    ModelFactory.createUniverse("uses-default", customer.getId());
    Universe customUniverse = ModelFactory.createUniverse("uses-custom", customer.getId());
    setPrimaryImageBundle(customUniverse, provider, customBundle.getUuid());

    assertEquals(1, defaultBundle.getUniverseCount());
    assertEquals(1, customBundle.getUniverseCount());
  }

  @Test
  public void testGetUniverseCountSkipsClustersOnOtherProviders() {
    Customer customer = ModelFactory.testCustomer();
    Provider aws = ModelFactory.awsProvider(customer);
    Provider gcp = ModelFactory.gcpProvider(customer);
    ImageBundle awsDefault = ImageBundle.create(aws, "aws-default", new ImageBundleDetails(), true);
    ImageBundle awsCustom = ImageBundle.create(aws, "aws-custom", new ImageBundleDetails(), false);
    ImageBundle gcpDefault = ImageBundle.create(gcp, "gcp-default", new ImageBundleDetails(), true);
    ImageBundle gcpCustom = ImageBundle.create(gcp, "gcp-custom", new ImageBundleDetails(), false);

    Universe customUniverse = ModelFactory.createUniverse("mixed-custom", customer.getId());
    setPrimaryImageBundle(customUniverse, aws, awsCustom.getUuid());
    addReadReplica(customUniverse, gcp, gcpCustom.getUuid());

    // Primary omits the bundle, so it uses the AWS default. The read replica is on GCP and
    // must not be treated as that default.
    Universe defaultUniverse = ModelFactory.createUniverse("mixed-default", customer.getId());
    addReadReplica(defaultUniverse, gcp, null);

    assertEquals(1, awsDefault.getUniverseCount());
    assertEquals(1, awsCustom.getUniverseCount());
    assertEquals(1, gcpDefault.getUniverseCount());
    assertEquals(1, gcpCustom.getUniverseCount());
  }

  private static void addReadReplica(Universe universe, Provider provider, UUID imageBundleUUID) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    UserIntent readReplica = new UserIntent();
    readReplica.universeName = details.getPrimaryCluster().userIntent.universeName;
    TestUtils.initUserIntent(
            readReplica,
            provider,
            ApiUtils.UTIL_INST_TYPE,
            ApiUtils.getDummyDeviceInfo(1, 100),
            "demo-access")
        .setImageBundleUUID(imageBundleUUID);
    details.upsertCluster(readReplica, null, null, UUID.randomUUID());
    universe.setUniverseDetails(details);
    universe.update();
  }

  private static void setPrimaryImageBundle(
      Universe universe, Provider provider, UUID imageBundleUUID) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    TestUtils.getProviderInitializerForTests(
            details.getPrimaryCluster().userIntent, provider.getUuid())
        .setImageBundleUUID(imageBundleUUID);
    universe.setUniverseDetails(details);
    universe.update();
  }
}
