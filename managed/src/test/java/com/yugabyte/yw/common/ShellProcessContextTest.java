// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Test;

public class ShellProcessContextTest extends FakeDBApplication {

  @Test
  public void redactCommandMasksTelemetryCredentials() {
    Customer customer = ModelFactory.testCustomer();
    AWSCloudWatchConfig config = new AWSCloudWatchConfig();
    config.setType(ProviderType.AWS_CLOUDWATCH);
    config.setAccessKey("AKIAEXAMPLEACCESSKEY");
    config.setSecretKey("example/secret+key/0123456789");
    TelemetryProvider telemetryProvider = new TelemetryProvider();
    telemetryProvider.setUuid(UUID.randomUUID());
    telemetryProvider.setCustomerUUID(customer.getUuid());
    telemetryProvider.setName("cloudwatch");
    telemetryProvider.setTags(ImmutableMap.of());
    telemetryProvider.setConfig(config);
    telemetryProvider.save();

    List<String> command =
        new ArrayList<>(
            Arrays.asList(
                "bin/ybcloud.sh",
                "--some_flag",
                "prefix-" + config.getSecretKey(),
                "--other_flag",
                config.getAccessKey()));
    String logged = String.join(" ", ShellProcessContext.builder().build().redactCommand(command));

    assertFalse(logged, logged.contains(config.getAccessKey()));
    assertFalse(logged, logged.contains(config.getSecretKey()));
    assertTrue("the command that runs is unchanged", command.contains(config.getAccessKey()));
  }
}
