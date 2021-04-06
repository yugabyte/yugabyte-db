// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

public class PriceComponentTest extends FakeDBApplication {
  private Customer testCustomer;
  private Provider testProvider;
  private Region testRegion;
  private String originalEffectiveDate = "2017-02-01T00:00:00Z";

  private PriceComponent.PriceDetails getValidPriceDetails() {
    PriceComponent.PriceDetails details = new PriceComponent.PriceDetails();
    details.unit = PriceComponent.PriceDetails.Unit.Hours;
    details.pricePerUnit = 0.25;
    details.currency = PriceComponent.PriceDetails.Currency.USD;
    details.effectiveDate = originalEffectiveDate;
    details.description = "foobar";
    return details;
  }

  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testProvider = ModelFactory.awsProvider(testCustomer);
    testRegion = Region.create(testProvider, "region-1", "Region 1", "yb-image-1");
  }

  @Test
  public void testCreate() {
    PriceComponent.PriceDetails details = getValidPriceDetails();
    PriceComponent.upsert(testProvider.uuid, testRegion.code, "foo", details);
    PriceComponent component = PriceComponent.get(testProvider.uuid, testRegion.code, "foo");

    assertNotNull(component);
    assertEquals("aws", component.getProviderCode());
    assertEquals(testRegion.code, component.getRegionCode());
    assertEquals("foo", component.getComponentCode());
    assertEquals(details.pricePerUnit, component.priceDetails.pricePerUnit, 0.0);
    assertEquals(details.currency, component.priceDetails.currency);
    assertEquals(details.effectiveDate, originalEffectiveDate);
    assertEquals(details.description, component.priceDetails.description);
    assertEquals(details.unit, component.priceDetails.unit);
  }

  @Test
  public void testEdit() {
    PriceComponent.PriceDetails details = getValidPriceDetails();
    PriceComponent.upsert(testProvider.uuid, testRegion.code, "foo", details);
    PriceComponent component = PriceComponent.get(testProvider.uuid, testRegion.code, "foo");

    assertNotNull(component);
    assertEquals("aws", component.getProviderCode());
    assertEquals(testRegion.code, component.getRegionCode());
    assertEquals("foo", component.getComponentCode());
    assertEquals(details.pricePerUnit, component.priceDetails.pricePerUnit, 0.0);
    assertEquals(details.currency, component.priceDetails.currency);
    assertEquals(details.effectiveDate, originalEffectiveDate);
    assertEquals(details.description, component.priceDetails.description);
    assertEquals(details.unit, component.priceDetails.unit);

    String nextEffectiveDate = "2017-02-22T00:00:00Z";
    details.effectiveDate = nextEffectiveDate;
    PriceComponent.upsert(testProvider.uuid, testRegion.code, "foo", details);
    component = PriceComponent.get(testProvider.uuid, testRegion.code, "foo");
    assertNotNull(component);
    assertEquals(details.effectiveDate, nextEffectiveDate);
  }
}
