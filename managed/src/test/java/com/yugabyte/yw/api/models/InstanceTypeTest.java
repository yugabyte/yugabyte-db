// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.models;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;

import java.util.List;

import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;


public class InstanceTypeTest extends FakeDBApplication {
  private Provider defaultProvider;
  @Before
  public void setUp() {
    defaultProvider = Provider.create("aws", "Amazon");
  }

  @Test
  public void testCreate() {
    InstanceType i1 = InstanceType.upsert(defaultProvider.code, "foo", 3, 10.0, 1, 100,
                                          InstanceType.VolumeType.EBS, null);
    assertNotNull(i1);
    assertEquals("aws", i1.getProviderCode());
    assertEquals("foo", i1.getInstanceTypeCode());
  }

  @Test
  public void testFindByProvider() {
    Provider newProvider = Provider.create("gce", "Google");
    InstanceType i1 = InstanceType.upsert(defaultProvider.code, "foo", 3, 10.0, 1, 100,
                                          InstanceType.VolumeType.EBS, null);
    InstanceType.upsert(newProvider.code, "bar", 2, 10.0, 1, 100,
                        InstanceType.VolumeType.EBS, null);
    List<InstanceType> instanceTypeList = InstanceType.findByProvider(defaultProvider);
    assertNotNull(instanceTypeList);
    assertEquals(1, instanceTypeList.size());
    assertThat(instanceTypeList.get(0).getInstanceTypeCode(),
               allOf(notNullValue(), equalTo("foo")));
  }
}
