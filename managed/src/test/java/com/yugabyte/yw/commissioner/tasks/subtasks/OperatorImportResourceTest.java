package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import play.inject.guice.GuiceApplicationBuilder;

@RunWith(MockitoJUnitRunner.class)
public class OperatorImportResourceTest extends CommissionerBaseTest {

  private OperatorUtils mockOperatorUtils;
  private CustomerConfigService mockCustomerConfigService;
  private Customer customer;

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    mockOperatorUtils = mock(OperatorUtils.class);
    mockCustomerConfigService = mock(CustomerConfigService.class);
    return super.configureApplication(builder)
        .overrides(bind(OperatorUtils.class).toInstance(mockOperatorUtils))
        .overrides(bind(ReleasesUtils.class).toInstance(mockReleasesUtils))
        .overrides(bind(CustomerConfigService.class).toInstance(mockCustomerConfigService));
  }

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    customer = defaultCustomer;
  }

  @Test
  public void testImportStorageConfig_deletedAssociatedUniverses_createsStorageConfigCr()
      throws Exception {
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(customer, "TEST_CONFIG");

    UUID deletedUniverseUUID1 = UUID.randomUUID();
    UUID deletedUniverseUUID2 = UUID.randomUUID();
    Set<UUID> associatedUUIDs = new HashSet<>();
    associatedUUIDs.add(deletedUniverseUUID1);
    associatedUUIDs.add(deletedUniverseUUID2);
    when(mockCustomerConfigService.getAssociatedUniverseUUIDS(any())).thenReturn(associatedUUIDs);

    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.resourceType = OperatorImportResource.Params.ResourceType.STORAGE_CONFIG;
    params.storageConfigUUID = storageConfig.getConfigUUID();
    params.namespace = "test-namespace";

    OperatorImportResource task = AbstractTaskBase.createTask(OperatorImportResource.class);
    task.initialize(params);
    task.run();

    ArgumentCaptor<CustomerConfig> configCaptor = ArgumentCaptor.forClass(CustomerConfig.class);
    verify(mockOperatorUtils)
        .createStorageConfigCr(configCaptor.capture(), eq("test-namespace"), any());
    assertEquals(storageConfig.getConfigUUID(), configCaptor.getValue().getConfigUUID());
  }
}
