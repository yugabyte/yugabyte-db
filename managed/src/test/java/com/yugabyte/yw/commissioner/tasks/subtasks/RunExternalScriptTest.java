// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.hamcrest.core.StringEndsWith.endsWith;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.ExternalScriptHelper;
import com.yugabyte.yw.models.helpers.ExternalScriptHelper.ExternalScriptConfObject;
import java.io.File;
import java.io.IOException;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RunExternalScriptTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Users defaultUser;

  private final String PLATFORM_URL = "";
  private final String TIME_LIMIT_MINS = "5";

  static String TMP_DEVOPS_HOME = "/tmp/yugaware_tests/RunExternalScriptTest";

  private SettableRuntimeConfigFactory settableRuntimeConfigFactory;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    new File(TMP_DEVOPS_HOME).mkdirs();
    settableRuntimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    final ObjectMapper mapper = new ObjectMapper();
    try {
      ExternalScriptConfObject runtimeConfigObject =
          new ExternalScriptConfObject("The script", "", UUID.randomUUID().toString());
      String json = mapper.writeValueAsString(runtimeConfigObject);
      settableRuntimeConfigFactory
          .forUniverse(defaultUniverse)
          .setValue(ExternalScriptHelper.EXT_SCRIPT_RUNTIME_CONFIG_PATH, json);
      settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.devops.home", TMP_DEVOPS_HOME);
    } catch (Exception e) {
      assertNull(e);
    }
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_DEVOPS_HOME));
  }

  @Test
  public void testRunFunctionality() {
    RunExternalScript.Params params = new RunExternalScript.Params();
    params.platformUrl = PLATFORM_URL;
    params.timeLimitMins = TIME_LIMIT_MINS;
    params.customerUUID = defaultCustomer.getUuid();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.userUUID = defaultUser.getUuid();
    when(mockShellProcessHandler.run(anyList(), anyMap(), anyString()))
        .thenReturn(new ShellResponse());
    RunExternalScript externalScriptTask = AbstractTaskBase.createTask(RunExternalScript.class);
    externalScriptTask.initialize(params);
    externalScriptTask.run();
  }

  @Test
  public void testRunFunctionalityWithOldRuntimeConfig() {
    RunExternalScript.Params params = new RunExternalScript.Params();
    params.platformUrl = PLATFORM_URL;
    params.timeLimitMins = TIME_LIMIT_MINS;
    params.customerUUID = defaultCustomer.getUuid();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.userUUID = defaultUser.getUuid();
    settableRuntimeConfigFactory
        .forUniverse(defaultUniverse)
        .deleteEntry(ExternalScriptHelper.EXT_SCRIPT_RUNTIME_CONFIG_PATH);
    RunExternalScript externalScriptTask = AbstractTaskBase.createTask(RunExternalScript.class);
    externalScriptTask.initialize(params);
    RuntimeException re = assertThrows(RuntimeException.class, externalScriptTask::run);
    assertThat(re.getMessage(), endsWith(RunExternalScript.STALE_TASK_RAN_ERR));
    settableRuntimeConfigFactory
        .forUniverse(defaultUniverse)
        .setValue("platform_ext_script_content", "the script");
    settableRuntimeConfigFactory
        .forUniverse(defaultUniverse)
        .setValue("platform_ext_script_params", "");
    settableRuntimeConfigFactory
        .forUniverse(defaultUniverse)
        .setValue("platform_ext_script_schedule", UUID.randomUUID().toString());
    when(mockShellProcessHandler.run(anyList(), anyMap(), anyString()))
        .thenReturn(new ShellResponse());
    RunExternalScript externalScriptTask2 = AbstractTaskBase.createTask(RunExternalScript.class);
    externalScriptTask2.initialize(params);
    externalScriptTask2.run();
  }
}
