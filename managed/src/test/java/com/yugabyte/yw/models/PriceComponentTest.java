// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.helpers.ProviderAndRegion;
import java.util.List;
import java.util.Map;
import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Test;

public class PriceComponentTest extends FakeDBApplication {
  private static final Map<String, PriceComponent.PriceDetails.Unit> EXPECTED_UNIT_STRINGS =
      ImmutableMap.<String, PriceComponent.PriceDetails.Unit>builder()
          .put("GB-Mo", PriceComponent.PriceDetails.Unit.GBMonth)
          .put("GBMonth", PriceComponent.PriceDetails.Unit.GBMonth)
          .put("Hrs", PriceComponent.PriceDetails.Unit.Hours)
          .put("Hours", PriceComponent.PriceDetails.Unit.Hours)
          .put("IOPS-MO", PriceComponent.PriceDetails.Unit.PIOPMonth)
          .put("GIBPS-MO", PriceComponent.PriceDetails.Unit.GiBpsMonth)
          .build();
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
    PriceComponent.upsert(testProvider.getUuid(), testRegion.getCode(), "foo", details);
    PriceComponent component =
        PriceComponent.get(testProvider.getUuid(), testRegion.getCode(), "foo");

    assertNotNull(component);
    assertEquals("aws", component.getProviderCode());
    assertEquals(testRegion.getCode(), component.getRegionCode());
    assertEquals("foo", component.getComponentCode());
    assertEquals(details.pricePerUnit, component.getPriceDetails().pricePerUnit, 0.0);
    assertEquals(details.currency, component.getPriceDetails().currency);
    assertEquals(details.effectiveDate, originalEffectiveDate);
    assertEquals(details.description, component.getPriceDetails().description);
    assertEquals(details.unit, component.getPriceDetails().unit);
  }

  @Test
  public void testEdit() {
    PriceComponent.PriceDetails details = getValidPriceDetails();
    PriceComponent.upsert(testProvider.getUuid(), testRegion.getCode(), "foo", details);
    PriceComponent component =
        PriceComponent.get(testProvider.getUuid(), testRegion.getCode(), "foo");

    assertNotNull(component);
    assertEquals("aws", component.getProviderCode());
    assertEquals(testRegion.getCode(), component.getRegionCode());
    assertEquals("foo", component.getComponentCode());
    assertEquals(details.pricePerUnit, component.getPriceDetails().pricePerUnit, 0.0);
    assertEquals(details.currency, component.getPriceDetails().currency);
    assertEquals(details.effectiveDate, originalEffectiveDate);
    assertEquals(details.description, component.getPriceDetails().description);
    assertEquals(details.unit, component.getPriceDetails().unit);

    String nextEffectiveDate = "2017-02-22T00:00:00Z";
    details.effectiveDate = nextEffectiveDate;
    PriceComponent.upsert(testProvider.getUuid(), testRegion.getCode(), "foo", details);
    component = PriceComponent.get(testProvider.getUuid(), testRegion.getCode(), "foo");
    assertNotNull(component);
    assertEquals(details.effectiveDate, nextEffectiveDate);
  }

  @Test
  public void testSetUnitFromString() {
    for (String unitStr : EXPECTED_UNIT_STRINGS.keySet()) {
      PriceComponent.PriceDetails priceDetails = new PriceComponent.PriceDetails();
      priceDetails.setUnitFromString(unitStr);
      assertThat(priceDetails.unit, equalTo(EXPECTED_UNIT_STRINGS.get(unitStr)));
    }

    PriceComponent.PriceDetails priceDetails = new PriceComponent.PriceDetails();
    priceDetails.setUnitFromString("Wrong");
    assertThat(priceDetails.unit, nullValue());
  }

  @Test
  public void testFindByProviderAndRegion() {
    PriceComponent.PriceDetails details = getValidPriceDetails();
    PriceComponent pc1 =
        PriceComponent.upsert(testProvider.getUuid(), testRegion.getCode(), "foo", details);
    PriceComponent pc2 = PriceComponent.upsert(testProvider.getUuid(), "code2", "bar", details);
    PriceComponent pc3 = PriceComponent.upsert(testProvider.getUuid(), "other", "foo", details);

    List<PriceComponent> components =
        PriceComponent.findByProvidersAndRegions(
            ImmutableList.of(
                new ProviderAndRegion(testProvider.getUuid(), testRegion.getCode()),
                new ProviderAndRegion(testProvider.getUuid(), "code2")));

    assertThat(components, Matchers.containsInAnyOrder(pc1, pc2));
  }
}
