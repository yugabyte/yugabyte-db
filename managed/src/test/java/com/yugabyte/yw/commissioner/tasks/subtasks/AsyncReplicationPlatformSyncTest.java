package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.cdc.CdcConsumer;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.Master;

import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class AsyncReplicationPlatformSyncTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe source, target;

  @Mock private YBClient mockClient;
  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  private AsyncReplicationPlatformSync task;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    source = ModelFactory.createUniverse("source", defaultCustomer.getCustomerId());
    target = ModelFactory.createUniverse("target", defaultCustomer.getCustomerId());

    // Setup mock cluster config with an async replication relationship
    try {
      CdcConsumer.StreamEntryPB relationship =
          CdcConsumer.StreamEntryPB.newBuilder()
              .setConsumerTableId("targetTableId")
              .setProducerTableId("sourceTableId")
              .build();

      Master.SysClusterConfigEntryPB config =
          Master.SysClusterConfigEntryPB.newBuilder()
              .setVersion(2)
              .setClusterUuid(target.universeUUID.toString())
              .setConsumerRegistry(
                  CdcConsumer.ConsumerRegistryPB.newBuilder()
                      .putProducerMap(
                          source.universeUUID.toString(),
                          CdcConsumer.ProducerEntryPB.newBuilder()
                              .putStreamMap("async_replication_relationship", relationship)
                              .build()))
              .build();

      GetMasterClusterConfigResponse mockConfigResponse =
          new GetMasterClusterConfigResponse(1111, "", config, null);

      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
    } catch (Exception e) {
    }

    when(baseTaskDependencies.getRuntimeConfigFactory()).thenReturn(runtimeConfigFactory);
    when(mockService.getClient(any(), any())).thenReturn(mockClient);

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.universeUUID = target.universeUUID;

    task = new AsyncReplicationPlatformSync(baseTaskDependencies);
    task.initialize(params);
  }

  @After
  public void tearDown() {
    source.delete();
    target.delete();
    defaultCustomer.delete();
  }

  @Test
  public void testRelationshipExistsInTableAndConfig() {
    // async replication relationship exists in both the cluster config and in the table
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(source, "sourceTableId", target, "targetTableId", true);

    task.run();
    target.refresh();

    // Relationship should still exist
    assertEquals(1, target.targetAsyncReplicationRelationships.size());
    assertEquals(
        relationship, target.targetAsyncReplicationRelationships.stream().findFirst().orElse(null));
  }

  @Test
  public void testRelationshipExistsInConfigNotTable() {
    // No relationship exists in the table
    task.run();
    target.refresh();

    // Check that relationship has been created to match the cluster config entry
    assertEquals(1, target.targetAsyncReplicationRelationships.size());

    AsyncReplicationRelationship relationship =
        target.targetAsyncReplicationRelationships.stream().findFirst().orElse(null);

    assertNotNull(relationship);
    assertEquals(relationship.sourceUniverse.universeUUID, source.universeUUID);
    assertEquals(relationship.sourceTableID, "sourceTableId");
    assertEquals(relationship.targetUniverse.universeUUID, target.universeUUID);
    assertEquals(relationship.targetTableID, "targetTableId");
    assertTrue(relationship.active);
  }

  @Test
  public void testRelationshipExistsInTableNotConfig() {
    // async replication relationship exists in the table but not the cluster config
    AsyncReplicationRelationship relationship =
        AsyncReplicationRelationship.create(
            source, "sourceTableId2", target, "targetTableId2", true);

    task.run();
    target.refresh();

    // Check that the relationship in the table has been deleted
    assertFalse(target.targetAsyncReplicationRelationships.contains(relationship));
  }
}
