// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.api.models;

import com.google.common.collect.Sets;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.UniverseDetails;
import org.junit.Before;
import org.junit.Test;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;

public class UniverseTest extends FakeDBApplication {
  private Provider defaultProvider;
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = Customer.create("Test", "test@test.com", "foo");
    defaultProvider = Provider.create("Amazon");
  }

  @Test
  public void testCreate() {
    Universe u = Universe.create("Test Universe", defaultCustomer.customerId);
    assertNotNull(u);
    assertThat(u.universeUUID, is(allOf(notNullValue(), equalTo(u.universeUUID))));
    assertThat(u.version, is(allOf(notNullValue(), equalTo(1))));
    assertThat(u.name, is(allOf(notNullValue(), equalTo("Test Universe"))));
    assertThat(u.getUniverseDetails(), is(notNullValue()));
  }

  @Test
  public void testGetSingleUniverse() {
    Universe newUniverse = Universe.create("Test Universe", defaultCustomer.customerId);
    assertNotNull(newUniverse);
    Universe fetchedUniverse = Universe.get(newUniverse.universeUUID);
    assertNotNull(fetchedUniverse);
    assertEquals(fetchedUniverse, newUniverse);
  }

  @Test
  public void testGetMultipleUniverse() {
    Universe u1 = Universe.create("Universe1", defaultCustomer.customerId);
    Universe u2 = Universe.create("Universe2", defaultCustomer.customerId);
    Universe u3 = Universe.create("Universe3", defaultCustomer.customerId);
    Set<UUID> uuids = Sets.newHashSet(u1.universeUUID, u2.universeUUID, u3.universeUUID);

    Set<Universe> universes = Universe.get(uuids);
    assertNotNull(universes);
    assertEquals(universes.size(), 3);
  }

  @Test(expected = RuntimeException.class)
  public void testGetUnknownUniverse() {
    UUID unknownUUID = UUID.randomUUID();
    Universe u = Universe.get(unknownUUID);
  }

  @Test
  public void testSaveDetails() {
    Universe u = Universe.create("Test Universe", defaultCustomer.customerId);

    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();

        // Create some subnets.
        List<String> subnets = new ArrayList<String>();
        subnets.add("subnet-1");
        subnets.add("subnet-2");
        subnets.add("subnet-3");

        // Add a desired number of nodes.
        universeDetails.numNodes = 5;
        for (int idx = 1; idx <= universeDetails.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.instance_name = "host-n" + idx;
          node.cloud = "aws";
          node.az = "az-" + idx;
          node.region = "test-region";
          node.subnet_id = subnets.get(idx % subnets.size());
          node.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsMap.put(node.instance_name, node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
    u = Universe.saveDetails(u.universeUUID, updater);

    int idx = 1;
    for (NodeDetails node : u.getMasters()) {
      assertTrue(node.isMaster);
      assertThat(node.instance_name, is(allOf(notNullValue(), equalTo("host-n" + idx))));
      idx++;
    }

    idx = 1;
    for (NodeDetails node : u.getTServers()) {
      assertTrue(node.isTserver);
      assertThat(node.instance_name, is(allOf(notNullValue(), equalTo("host-n" + idx))));
      idx++;
    }

    assertTrue(u.getTServers().size() > u.getMasters().size());
    assertEquals(u.getMasters().size(), 3);
    assertEquals(u.getTServers().size(), 5);
  }

  @Test
  public void testGetMasterAddresses() {
    Universe u = Universe.create("Test Universe", defaultCustomer.customerId);

    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();

        // Add a desired number of nodes.
        universeDetails.numNodes = 3;
        for (int idx = 1; idx <= universeDetails.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.instance_name = "host-n" + idx;
          node.cloud = "aws";
          node.az = "az-" + idx;
          node.region = "test-region";
          node.subnet_id = "subnet-" + idx;
          node.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsMap.put(node.instance_name, node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
    u = Universe.saveDetails(u.universeUUID, updater);
    assertThat(u.getMasterAddresses(), is(allOf(notNullValue(), equalTo("host-n1:7100,host-n2:7100,host-n3:7100"))));
  }
}
