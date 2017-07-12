// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.InstanceType.VolumeType;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;

import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyString;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.mock;

import com.yugabyte.yw.common.FakeDBApplication;
import play.libs.Json;

public class InstanceTypeTest extends FakeDBApplication {
  private Provider defaultProvider;
  private Customer defaultCustomer;
  private InstanceType.InstanceTypeDetails defaultDetails;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeSizeGB = 100;
    volumeDetails.volumeType = InstanceType.VolumeType.EBS;
    defaultDetails = new InstanceType.InstanceTypeDetails();
    defaultDetails.volumeDetailsList.add(volumeDetails);
    defaultDetails.setDefaultMountPaths();
  }

  @Test
  public void testCreate() {
    InstanceType i1 = InstanceType.upsert(defaultProvider.code, "foo", 3, 10.0, defaultDetails);
    assertNotNull(i1);
    assertEquals("aws", i1.getProviderCode());
    assertEquals("foo", i1.getInstanceTypeCode());
  }
  
  @Test
  public void testSetDefaultVolumeDetailsList() {
    InstanceType.InstanceTypeDetails d1 = new InstanceType.InstanceTypeDetails();
    d1.setDefaultVolumeDetailsList();
    assertNotNull(d1.volumeDetailsList);
    for (int i = 0; i < d1.volumeDetailsList.size(); i++) {
    	InstanceType.VolumeDetails v1 = d1.volumeDetailsList.get(i);
    	assertEquals(250, v1.volumeSizeGB.intValue());
    	assertEquals(InstanceType.VolumeType.EBS, v1.volumeType);
    	assertEquals(String.format("/mnt/d%d", i), v1.mountPath); 
    }
  }
  
  @Test
  public void testGetInstanceTypeDetails() {
  	Map<String, List<InstanceType.InstanceTypeRegionDetails>> mockMap = mock(HashMap.class);
  	InstanceType.InstanceTypeDetails itd1 = InstanceType.InstanceTypeDetails.getInstanceTypeDetails(mockMap);
  	assertNotNull(itd1);
  	assertNotNull(itd1.volumeDetailsList);
  	assertEquals(mockMap, itd1.regionCodeToDetailsMap);
    for (int i = 0; i < itd1.volumeDetailsList.size(); i++) {
    	InstanceType.VolumeDetails v1 = itd1.volumeDetailsList.get(i);
    	assertEquals(250, v1.volumeSizeGB.intValue());
    	assertEquals(InstanceType.VolumeType.EBS, v1.volumeType);
    	assertEquals(String.format("/mnt/d%d", i), v1.mountPath); 
    }
  }

  @Test
  public void testFindByProvider() {
    Provider newProvider = ModelFactory.gcpProvider(defaultCustomer);
    InstanceType i1 = InstanceType.upsert(defaultProvider.code, "foo", 3, 10.0, defaultDetails);
    InstanceType.upsert(newProvider.code, "bar", 2, 10.0, defaultDetails);
    List<InstanceType> instanceTypeList = InstanceType.findByProvider(defaultProvider);
    assertNotNull(instanceTypeList);
    assertEquals(1, instanceTypeList.size());
    assertThat(instanceTypeList.get(0).getInstanceTypeCode(),
               allOf(notNullValue(), equalTo("foo")));
  }

  @Test
  public void testDeleteByProviderWithValidProviderCode() {
    Provider newProvider = ModelFactory.gcpProvider(defaultCustomer);
    InstanceType.upsert(newProvider.code, "bar", 2, 10.0, defaultDetails);
    int response = InstanceType.deleteInstanceTypesForProvider(newProvider.code);
    List<InstanceType> instanceTypeList = InstanceType.findByProvider(newProvider);
    assertEquals(1, response);
    assertEquals(0, instanceTypeList.size());

  }

  @Test
  public void testDeleteByProviderWithInvalidProviderCode() {
    Provider newProvider = ModelFactory.gcpProvider(defaultCustomer);
    InstanceType.upsert(newProvider.code, "bar", 2, 10.0, defaultDetails);
    int response = InstanceType.deleteInstanceTypesForProvider("abcd");
    List<InstanceType> instanceTypeList = InstanceType.findByProvider(newProvider);
    assertEquals(0, response);
    assertEquals(1, instanceTypeList.size());
  }

  @Test
  public void testCreateWithValidMetadata() {
    ObjectNode metaData = Json.newObject();
    metaData.put("numCores", 4);
    metaData.put("memSizeGB", 300);
    InstanceType.InstanceTypeDetails instanceTypeDetails = new InstanceType.InstanceTypeDetails();
    instanceTypeDetails.volumeDetailsList = new ArrayList<>();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeSizeGB = 20;
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    instanceTypeDetails.volumeDetailsList.add(volumeDetails);

    metaData.put("longitude", -119.417932);
    metaData.put("ybImage", "yb-image-1");
    metaData.set("instanceTypeDetails", Json.toJson(instanceTypeDetails));
    InstanceType it = InstanceType.createWithMetadata(defaultProvider, "it-1", metaData);
    assertNotNull(it);
    JsonNode itJson = Json.toJson(it);
    assertValue(itJson, "providerCode", "aws");
    assertValue(itJson, "instanceTypeCode", "it-1");
    assertValue(itJson, "numCores", "4");
    assertValue(itJson, "memSizeGB", "300.0");
    JsonNode volumeDetailsList = itJson.get("instanceTypeDetails").get("volumeDetailsList");
    assertTrue(volumeDetailsList.isArray());
    assertEquals(1, volumeDetailsList.size());
    assertValue(volumeDetailsList.get(0), "volumeSizeGB", "20");
    assertValue(volumeDetailsList.get(0), "volumeType", "SSD");
  }
}
