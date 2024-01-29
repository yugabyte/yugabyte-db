package com.yugabyte.yw.common.cdc;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.CDCReplicationSlotResponse;
import com.yugabyte.yw.models.Universe;
import java.util.*;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.CDCStreamInfo;
import org.yb.client.ListCDCStreamsResponse;
import org.yb.client.ListNamespacesResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.NamespaceIdentifierPB;

@RunWith(MockitoJUnitRunner.class)
public class CdcStreamManagerTest {
  private YBClient mockClient;
  private CdcStreamManager streamManager;

  @Before
  public void setup() {
    mockClient = mock(YBClient.class);
    YBClientService mockClientService = mock(YBClientService.class);

    when(mockClientService.getClient(any(), any())).thenReturn(mockClient);

    streamManager = new CdcStreamManager(mockClientService);
  }

  @Test
  public void getAllCdcStreams_valid() throws Exception {
    String streamId = UUID.randomUUID().toString();
    String namespaceId = UUID.randomUUID().toString();
    Map<String, String> options = new HashMap<>();
    options.put("a", "b");
    options.put("b", "c");

    CDCStreamInfo mockStream = mock(CDCStreamInfo.class);
    when(mockStream.getStreamId()).thenReturn(streamId);
    when(mockStream.getOptions()).thenReturn(options);
    when(mockStream.getNamespaceId()).thenReturn(namespaceId);

    ListCDCStreamsResponse response = mock(ListCDCStreamsResponse.class);
    when(response.hasError()).thenReturn(false);
    when(response.getStreams())
        .thenReturn(
            new ArrayList<CDCStreamInfo>() {
              {
                add(mockStream);
              }
            });
    when(mockClient.listCDCStreams(null, null, MasterReplicationOuterClass.IdTypePB.NAMESPACE_ID))
        .thenReturn(response);

    List<CdcStream> r = streamManager.getAllCdcStreams(mock(Universe.class));

    assertEquals(1, r.size());
    assertEquals(streamId, r.get(0).getStreamId());
    assertEquals(2, r.get(0).getOptions().size());
    assertEquals(namespaceId, r.get(0).getNamespaceId());
  }

  @Test
  public void getAllCdcStreams_error() throws Exception {
    ListCDCStreamsResponse response = mock(ListCDCStreamsResponse.class);
    when(response.hasError()).thenReturn(true);
    when(response.errorMessage()).thenReturn("err");
    when(mockClient.listCDCStreams(null, null, MasterReplicationOuterClass.IdTypePB.NAMESPACE_ID))
        .thenReturn(response);

    assertThrows(
        "err", Exception.class, () -> streamManager.getAllCdcStreams(mock(Universe.class)));
  }

  @Test
  public void getFirstTable_negative() throws Exception {
    ListTablesResponse listTables = mock(ListTablesResponse.class);
    when(listTables.getTableInfoList()).thenReturn(new ArrayList<>());
    when(mockClient.getTablesList()).thenReturn(listTables);

    assertThrows(
        "Must have at least 1 table in database.",
        Exception.class,
        () -> streamManager.getFirstTable(mockClient, ""));
  }

  @Test
  public void testListReplicationSlotError() throws Exception {
    ListCDCStreamsResponse response = mock(ListCDCStreamsResponse.class);
    when(response.hasError()).thenReturn(true);
    when(response.errorMessage()).thenReturn("err");
    when(mockClient.listCDCStreams(null, null, MasterReplicationOuterClass.IdTypePB.NAMESPACE_ID))
        .thenReturn(response);

    assertThrows(
        "err", Exception.class, () -> streamManager.getAllCdcStreams(mock(Universe.class)));
  }

  @Test
  public void testListReplicationSlot() throws Exception {
    String streamId = UUID.randomUUID().toString();
    String namespaceId = UUID.randomUUID().toString();
    Map<String, String> options = new HashMap<>();
    options.put("state", "ACTIVE");

    CDCStreamInfo mockStream = mock(CDCStreamInfo.class);
    when(mockStream.getStreamId()).thenReturn(streamId);
    when(mockStream.getOptions()).thenReturn(options);
    when(mockStream.getNamespaceId()).thenReturn(namespaceId);
    when(mockStream.getCdcsdkYsqlReplicationSlotName()).thenReturn("test_slot");

    ListNamespacesResponse namespacesResponse = mock(ListNamespacesResponse.class);
    NamespaceIdentifierPB mockNamespaceIdentifierPB = mock(NamespaceIdentifierPB.class);
    when(mockNamespaceIdentifierPB.getId()).thenReturn(ByteString.copyFrom(namespaceId.getBytes()));
    when(mockNamespaceIdentifierPB.getName()).thenReturn("test_database");
    when(namespacesResponse.getNamespacesList())
        .thenReturn(Collections.singletonList(mockNamespaceIdentifierPB));

    when(mockClient.getNamespacesList()).thenReturn(namespacesResponse);

    ListCDCStreamsResponse response = mock(ListCDCStreamsResponse.class);
    when(response.hasError()).thenReturn(false);
    when(response.getStreams())
        .thenReturn(
            new ArrayList<CDCStreamInfo>() {
              {
                add(mockStream);
              }
            });
    when(mockClient.listCDCStreams(null, null, MasterReplicationOuterClass.IdTypePB.NAMESPACE_ID))
        .thenReturn(response);

    CDCReplicationSlotResponse r = streamManager.listReplicationSlot(mock(Universe.class));

    assertEquals(1, r.replicationSlots.size());
    assertEquals("test_database", r.getReplicationSlots().get(0).databaseName);
    assertEquals("test_slot", r.getReplicationSlots().get(0).slotName);
    assertEquals(streamId, r.getReplicationSlots().get(0).streamID);
    assertEquals("ACTIVE", r.getReplicationSlots().get(0).state);
  }
}
