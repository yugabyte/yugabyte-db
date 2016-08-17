// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.models;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import org.junit.Before;
import org.junit.Test;

import javax.persistence.PersistenceException;

import java.util.List;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;


public class InstanceTypeTest extends FakeDBApplication {
  private Provider defaultProvider;
  @Before
  public void setUp() {
    defaultProvider = Provider.create("aws", "Amazon");
  }

  @Test
  public void testCreate() {
    InstanceType i1 = InstanceType.create(defaultProvider.code, "foo", 3, 10.0, 100, InstanceType.VolumeType.EBS);
    assertNotNull(i1);
    assertEquals("aws", i1.getProviderCode());
    assertEquals("foo", i1.getInstanceTypeCode());
  }

  @Test(expected = PersistenceException.class)
  public void testCreateWithDuplicateEmail() {
    InstanceType i1 = InstanceType.create(defaultProvider.code, "foo", 3, 10.0, 100, InstanceType.VolumeType.EBS);
    i1.save();
    InstanceType i2 = InstanceType.create(defaultProvider.code, "foo", 2, 5.0, 80, InstanceType.VolumeType.EBS);
    i2.save();
  }

  @Test
  public void testFindByProvider() {
    Provider newProvider = Provider.create("gce", "Google");
    InstanceType i1 = InstanceType.create(defaultProvider.code, "foo", 3, 10.0, 100, InstanceType.VolumeType.EBS);
    InstanceType.create(newProvider.code, "bar", 2, 10.0, 100, InstanceType.VolumeType.EBS);
    List<InstanceType> instanceTypeList = InstanceType.findByProvider(defaultProvider);
    assertNotNull(instanceTypeList);
    assertEquals(1, instanceTypeList.size());
    assertThat(instanceTypeList.get(0).getInstanceTypeCode(), allOf( notNullValue(), equalTo("foo")));
  }
}
