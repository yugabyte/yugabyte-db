// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.cloud.GCPInitializer;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import org.junit.Before;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;
import play.test.WithApplication;

import java.util.Map;
import java.util.UUID;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

public abstract class CommissionerBaseTest extends WithApplication {
  protected AccessManager mockAccessManager;
  protected NetworkManager mockNetworkManager;
  protected ConfigHelper mockConfigHelper;
  protected AWSInitializer mockAWSInitializer;
  protected GCPInitializer mockGCPInitializer;
  protected YBClientService mockYBClient;
  protected NodeManager mockNodeManager;
  protected CloudQueryHelper mockCloudQueryHelper;

  Customer defaultCustomer;
  Provider defaultProvider;
  Provider gcpProvider;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    gcpProvider = ModelFactory.gcpProvider(defaultCustomer);
  }

  @Override
  protected Application provideApplication() {
    mockAccessManager = mock(AccessManager.class);
    mockNetworkManager = mock(NetworkManager.class);
    mockConfigHelper = mock(ConfigHelper.class);
    mockAWSInitializer = mock(AWSInitializer.class);
    mockGCPInitializer = mock(GCPInitializer.class);
    mockYBClient = mock(YBClientService.class);
    mockNodeManager = mock(NodeManager.class);
    mockCloudQueryHelper = mock(CloudQueryHelper.class);

    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(AccessManager.class).toInstance(mockAccessManager))
        .overrides(bind(NetworkManager.class).toInstance(mockNetworkManager))
        .overrides(bind(ConfigHelper.class).toInstance(mockConfigHelper))
        .overrides(bind(AWSInitializer.class).toInstance(mockAWSInitializer))
        .overrides(bind(GCPInitializer.class).toInstance(mockGCPInitializer))
        .overrides(bind(YBClientService.class).toInstance(mockYBClient))
        .overrides(bind(NodeManager.class).toInstance(mockNodeManager))
        .overrides(bind(CloudQueryHelper.class).toInstance(mockCloudQueryHelper))
        .build();
  }

  protected TaskInfo waitForTask(UUID taskUUID) throws InterruptedException {
    while(true) {
      TaskInfo taskInfo = TaskInfo.get(taskUUID);
      if (taskInfo.getTaskState() == TaskInfo.State.Success ||
          taskInfo.getTaskState() == TaskInfo.State.Failure) {
        return taskInfo;
      }
      Thread.sleep(1000);
    }
  }
}
