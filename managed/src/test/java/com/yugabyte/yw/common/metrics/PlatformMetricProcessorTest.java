/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.metrics;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.List;
import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class PlatformMetricProcessorTest extends FakeDBApplication {

  Customer customer;
  @Mock MetricService metricService;
  @Captor private ArgumentCaptor<List<Metric>> metricsCaptor;

  PlatformMetricsProcessor platformMetricsProcessor;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    platformMetricsProcessor =
        new PlatformMetricsProcessor(null, null, metricService, new UniverseMetricProvider());
  }

  @Test
  public void testMetricsBatching() {
    for (int i = 0; i < 40; i++) {
      ModelFactory.createUniverse("testUniverse" + i, customer.getCustomerId());
    }

    platformMetricsProcessor.scheduleRunner();

    verify(metricService, times(2)).cleanAndSave(metricsCaptor.capture(), anyList());

    for (List<Metric> metrics : metricsCaptor.getAllValues()) {
      assertThat(metrics.size(), Matchers.lessThanOrEqualTo(CommonUtils.DB_OR_CHAIN_TO_WARN));
    }
  }
}
