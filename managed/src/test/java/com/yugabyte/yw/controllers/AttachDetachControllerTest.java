// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static com.yugabyte.yw.common.ModelFactory.testUser;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.METHOD_NOT_ALLOWED;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.common.ModelFactory;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes;
import org.yb.Schema;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import play.libs.Json;
import play.mvc.Result;

public class AttachDetachControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private String detachEndpoint;
  private String mainUniverseName;
  private UUID mainUniverseUUID;
  private Universe mainUniverse;

  // Xcluster test setup.
  private String namespace1Name;
  private String namespace1Id;
  private String exampleTableID1;
  private String exampleTable1Name;
  private Set<String> exampleTables;
  private ObjectNode createXClusterRequestParams;

  @Before
  public void setUp() {
    customer = testCustomer("AttachDetachController-test-customer");
    user = testUser(customer);

    mainUniverseName = "AttachDetachController-test-universe";
    mainUniverseUUID = UUID.randomUUID();
    mainUniverse = createUniverse(mainUniverseName, mainUniverseUUID);
    detachEndpoint =
        "/api/customers/" + customer.uuid + "/universes/" + mainUniverseUUID.toString() + "/export";
  }

  @Test
  public void testInvalidXClusterDetach() {
    UUID taskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    // Set up simple xcluster config.
    String targetUniverseName = "AttachDetachController-test-universe-2";
    UUID targetUniverseUUID = UUID.randomUUID();
    Universe targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    String configName = "XClusterConfig";
    createXClusterRequestParams =
        Json.newObject()
            .put("name", configName)
            .put("sourceUniverseUUID", mainUniverseUUID.toString())
            .put("targetUniverseUUID", targetUniverseUUID.toString());

    namespace1Name = "ycql-namespace1";
    namespace1Id = UUID.randomUUID().toString();
    exampleTableID1 = "000030af000030008000000000004000";
    exampleTable1Name = "exampleTable1";
    exampleTables = new HashSet<>();
    exampleTables.add(exampleTableID1);

    ArrayNode tables = Json.newArray();
    for (String table : exampleTables) {
      tables.add(table);
    }
    createXClusterRequestParams.putArray("tables").addAll(tables);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient mockClient = mock(YBClient.class);
    when(mockService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate))
        .thenReturn(mockClient);

    GetTableSchemaResponse mockTableSchemaResponseTable1 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(Collections.emptyList()),
            namespace1Name,
            "exampleTableID1",
            exampleTableID1,
            null,
            true,
            CommonTypes.TableType.YQL_TABLE_TYPE,
            Collections.emptyList());

    try {
      lenient()
          .when(mockClient.getTableSchemaByUUID(exampleTableID1))
          .thenReturn(mockTableSchemaResponseTable1);
    } catch (Exception ignored) {
    }

    String xClusterApiEndpoint = "/api/customers/" + customer.uuid + "/xcluster_configs";

    ListTablesResponse mockListTablesResponse = mock(ListTablesResponse.class);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList = new ArrayList<>();
    // Adding table 1.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table1TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table1TableInfoBuilder.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    table1TableInfoBuilder.setId(ByteString.copyFromUtf8(exampleTableID1));
    table1TableInfoBuilder.setName(exampleTable1Name);
    table1TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName(namespace1Name)
            .setId(ByteString.copyFromUtf8(namespace1Id))
            .build());
    tableInfoList.add(table1TableInfoBuilder.build());
    try {
      when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
      when(mockClient.getTablesList(null, true, null)).thenReturn(mockListTablesResponse);
    } catch (Exception e) {
      e.printStackTrace();
    }

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("skipReleases", true);

    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", xClusterApiEndpoint, user.createAuthToken(), createXClusterRequestParams);
    assertOk(result);

    result = assertPlatformException(() -> detachUniverse(bodyJson));
    assertEquals(METHOD_NOT_ALLOWED, result.status());
  }

  private Result detachUniverse(JsonNode bodyJson) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", detachEndpoint, user.createAuthToken(), bodyJson);
  }
}
