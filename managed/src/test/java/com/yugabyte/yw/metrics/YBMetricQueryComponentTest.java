// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import static com.yugabyte.yw.metrics.YBMetricQueryComponent.Function.Average;
import static com.yugabyte.yw.metrics.YBMetricQueryComponent.Function.Sum;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;
import org.mockito.stubbing.OngoingStubbing;

@RunWith(MockitoJUnitRunner.class)
public class YBMetricQueryComponentTest extends FakeDBApplication {

  @InjectMocks YBMetricQueryComponent ybMetricQueryComponent;

  private class MockResultSet {
    List<Date> times;
    List<Long> values;
    List<Boolean> hasNexts;
  }

  // Takes a MockResultSet that contains the information to mock the iterator's
  // next values. MockResultSet.hasNexts is expected to have 2x + 1 the values of the
  // other members since it is queried twice in each call (three times in the first iteration).
  public List<ResultSet> setupCalculate(List<MockResultSet> mockResultSet) {
    List<ResultSet> allRS = new ArrayList<>();
    ResultSet rs1 = Mockito.mock(ResultSet.class);
    ResultSet rs2 = Mockito.mock(ResultSet.class);
    ResultSet rs3 = Mockito.mock(ResultSet.class);
    allRS.add(rs1);
    allRS.add(rs2);
    allRS.add(rs3);

    Row row1 = Mockito.mock(Row.class);
    Row row2 = Mockito.mock(Row.class);
    Row row3 = Mockito.mock(Row.class);

    OngoingStubbing timestamps1 = when(row1.getTimestamp("ts"));
    for (Date time : mockResultSet.get(0).times) {
      timestamps1 = timestamps1.thenReturn(time);
    }
    OngoingStubbing timestamps2 = when(row2.getTimestamp("ts"));
    for (Date time : mockResultSet.get(1).times) {
      timestamps2 = timestamps2.thenReturn(time);
    }
    OngoingStubbing timestamps3 = when(row3.getTimestamp("ts"));
    for (Date time : mockResultSet.get(2).times) {
      timestamps3 = timestamps3.thenReturn(time);
    }

    OngoingStubbing values1 = when(row1.getLong("value"));
    for (Long value : mockResultSet.get(0).values) {
      values1 = values1.thenReturn(value);
    }
    OngoingStubbing values2 = when(row2.getLong("value"));
    for (Long value : mockResultSet.get(1).values) {
      values2 = values2.thenReturn(value);
    }
    OngoingStubbing values3 = when(row3.getLong("value"));
    for (Long value : mockResultSet.get(2).values) {
      values3 = values3.thenReturn(value);
    }

    Iterator iterator1 = Mockito.mock(Iterator.class);
    Iterator iterator2 = Mockito.mock(Iterator.class);
    Iterator iterator3 = Mockito.mock(Iterator.class);

    when(rs1.iterator()).thenReturn(iterator1);
    when(rs2.iterator()).thenReturn(iterator2);
    when(rs3.iterator()).thenReturn(iterator3);

    OngoingStubbing iteratorNext1 = when(iterator1.hasNext());
    for (Boolean hasNext : mockResultSet.get(0).hasNexts) {
      iteratorNext1 = iteratorNext1.thenReturn(hasNext);
    }
    OngoingStubbing iteratorNext2 = when(iterator2.hasNext());
    for (Boolean hasNext : mockResultSet.get(1).hasNexts) {
      iteratorNext2 = iteratorNext2.thenReturn(hasNext);
    }
    OngoingStubbing iteratorNext3 = when(iterator3.hasNext());
    for (Boolean hasNext : mockResultSet.get(2).hasNexts) {
      iteratorNext3 = iteratorNext3.thenReturn(hasNext);
    }

    when(iterator1.next()).thenReturn(row1);
    when(iterator2.next()).thenReturn(row2);
    when(iterator3.next()).thenReturn(row3);

    return allRS;
  }

  @Test
  public void testRateCalculateSum() throws Exception {
    Long timeVal = 2323283232L;
    Long interval = 30000L;
    MockResultSet mockResultSet = new MockResultSet();
    mockResultSet.times =
        Arrays.asList(
            new Date(timeVal),
            new Date(timeVal - interval),
            new Date(timeVal - interval * 2),
            new Date(timeVal - interval * 3),
            new Date(timeVal - interval * 4));
    mockResultSet.values = Arrays.asList(16000L, 12000L, 10000L, 7000L, 5000L);
    mockResultSet.hasNexts =
        Arrays.asList(true, true, true, true, true, true, true, true, true, false);

    List<ResultSet> allRS =
        setupCalculate(Arrays.asList(mockResultSet, mockResultSet, mockResultSet));

    Map<Long, Double> expectedValues =
        ImmutableMap.of(
            2323163L, (200.0 / 3) * 3,
            2323193L, (300.0 / 3) * 3,
            2323223L, (200.0 / 3) * 3,
            2323253L, (400.0 / 3) * 3);
    assertEquals(expectedValues, ybMetricQueryComponent.calculateRate(allRS, Sum, 3));
  }

  @Test
  public void testRateCalculateAverage() throws Exception {
    Long timeVal = 2323283232L;
    Long interval = 30000L;
    MockResultSet mockResultSet = new MockResultSet();
    mockResultSet.times =
        Arrays.asList(
            new Date(timeVal),
            new Date(timeVal - interval),
            new Date(timeVal - interval * 2),
            new Date(timeVal - interval * 3),
            new Date(timeVal - interval * 4));
    mockResultSet.values = Arrays.asList(16000L, 12000L, 10000L, 7000L, 5000L);
    mockResultSet.hasNexts =
        Arrays.asList(true, true, true, true, true, true, true, true, true, false);

    List<ResultSet> allRS =
        setupCalculate(Arrays.asList(mockResultSet, mockResultSet, mockResultSet));

    Map<Long, Double> expectedValues =
        ImmutableMap.of(
            2323163L, 200.0 / 3,
            2323193L, 300.0 / 3,
            2323223L, 200.0 / 3,
            2323253L, 400.0 / 3);
    assertEquals(expectedValues, ybMetricQueryComponent.calculateRate(allRS, Average, 3));
  }

  @Test
  public void testRateCalculateSumValsMissing() {
    Long timeVal = 2323283232L;
    Long interval = 30000L;
    MockResultSet mockResultSet = new MockResultSet();
    MockResultSet mockResultSetMissing = new MockResultSet();
    mockResultSet.times =
        Arrays.asList(
            new Date(timeVal),
            new Date(timeVal - interval),
            new Date(timeVal - interval * 2),
            new Date(timeVal - interval * 3),
            new Date(timeVal - interval * 4));
    mockResultSetMissing.times =
        Arrays.asList(
            new Date(timeVal),
            new Date(timeVal - interval),
            new Date(timeVal - interval * 3),
            new Date(timeVal - interval * 4));

    mockResultSet.values = Arrays.asList(16000L, 12000L, 10000L, 7000L, 5000L);
    mockResultSetMissing.values = Arrays.asList(16000L, 12000L, 7000L, 5000L);

    mockResultSet.hasNexts =
        Arrays.asList(true, true, true, true, true, true, true, true, true, false);
    mockResultSetMissing.hasNexts = Arrays.asList(true, true, true, true, true, true, true, false);

    List<ResultSet> allRS =
        setupCalculate(Arrays.asList(mockResultSet, mockResultSet, mockResultSetMissing));

    Map<Long, Double> expectedValues =
        ImmutableMap.of(
            2323163L, (200.0 / 3) * 3,
            2323193L, (300.0 / 3) * 2 + (500.0 / 6),
            2323223L, (200.0 / 3) * 2,
            2323253L, (400.0 / 3) * 3);
    assertEquals(expectedValues, ybMetricQueryComponent.calculateRate(allRS, Sum, 3));
  }
}
