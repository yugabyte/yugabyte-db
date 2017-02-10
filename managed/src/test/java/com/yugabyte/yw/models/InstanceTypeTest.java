// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;

import java.util.List;

import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;


public class InstanceTypeTest extends FakeDBApplication {
  private Provider defaultProvider;
  private Customer defaultCustomer;
  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
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
    Provider newProvider = ModelFactory.gceProvider(defaultCustomer);
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
