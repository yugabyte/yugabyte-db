// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Matchers;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;

import java.util.Collection;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

import org.junit.After;
import org.junit.Before;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class PlacementInfoUtilTest extends FakeDBApplication {
  private Customer customer;
  private Universe universe;
  private final String univName = "Test Universe";
  private final UUID univUuid = UUID.randomUUID();
  private AvailabilityZone az1;
  private AvailabilityZone az2;
  private AvailabilityZone az3;

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    Universe.create(univName, univUuid, customer.getCustomerId());

    Provider p = Provider.create("aws", "Amazon");
    Region r1 = Region.create(p, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(p, "region-2", "Region 2", "yb-image-1");
    az1 = AvailabilityZone.create(r1, "PlacementAZ 1", "az-1", "subnet-1");
    az2 = AvailabilityZone.create(r1, "PlacementAZ 2", "az-2", "subnet-2");
    az3 = AvailabilityZone.create(r2, "PlacementAZ 3", "az-3", "subnet-3");
    List<UUID> regionList = new ArrayList<UUID>();
    regionList.add(r1.uuid);
    regionList.add(r2.uuid);

    UserIntent userIntent = new UserIntent();
    userIntent.universeName = univName;
    userIntent.replicationFactor = 3;
    userIntent.isMultiAZ = true;
    userIntent.numNodes = 3;
    userIntent.provider = "aws";
    userIntent.regionList = regionList;
    Universe.saveDetails(univUuid, ApiUtils.mockUniverseUpdater(userIntent));
    universe = Universe.get(univUuid);
  }

  @After
  public void tearDown() {
    universe.delete(univUuid);
  }

  private Universe.UniverseUpdater setAzUUIDs(UserIntent userIntent) {
    return new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        Set<NodeDetails> nodes = universe.getUniverseDetails().nodeDetailsSet;
        for (NodeDetails node : nodes) {
          switch (node.cloudInfo.az) {
          case "az-1":
            node.azUuid = az1.uuid;
            break;
          case "az-2":
            node.azUuid = az2.uuid;
            break;
          case "az-3":
            node.azUuid = az3.uuid;
            break;
          }
        }
        universeDetails.placementInfo =
          PlacementInfoUtil.getPlacementInfo(userIntent);
        universe.setUniverseDetails(universeDetails);
      }
    };
  }

  @Test
  public void testExpandPlacement() {
    UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
    ud.universeUUID = univUuid;
    Universe.saveDetails(univUuid, setAzUUIDs(ud.userIntent));
    ud.userIntent.numNodes = 5;
    universe = Universe.get(univUuid);
    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
    assertEquals(2, PlacementInfoUtil.getTserversToProvision(nodes).size());
  }

  @Test
  public void testEditPlacement() {
    UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
    ud.universeUUID = univUuid;
    Provider p = Provider.get("Amazon");
    Region r3 = Region.create(p, "region-3", "Region 3", "yb-image-3");
    AvailabilityZone az4 = AvailabilityZone.create(r3, "az-4", "PlacementAZ 4", "subnet-4");
    ud.userIntent.regionList.add(r3.uuid);

    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(3, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(3, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
    assertEquals(3, PlacementInfoUtil.getTserversToProvision(nodes).size());
    assertEquals(3, PlacementInfoUtil.getMastersToProvision(nodes).size());
  }
}
