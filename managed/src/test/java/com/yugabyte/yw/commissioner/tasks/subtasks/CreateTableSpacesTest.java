// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.ModelFactory.createFromConfig;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.generateTablespaceParams;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.AdditionalMatchers;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class CreateTableSpacesTest extends CommissionerBaseTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String FETCH_TABLESPACE_ANSWER = "CREATE TABLESPACE";

  @Override
  @Before
  public void setUp() {
    super.setUp();
    mockFetchTablespaceAnswer("com/yugabyte/yw/controllers/tablespaces_shell_response.txt");
  }

  @Test
  public void testCreateTableSpaces_HappyPath_AnyTsCreated() {
    Universe universe =
        createFromConfig(defaultProvider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    mockCreateTablespaceAnswer(ShellResponse.ERROR_CODE_SUCCESS, FETCH_TABLESPACE_ANSWER);
    verifyExecution(Success, universe.getUniverseUUID(), 3, "r1-az1-1;r1-az2-1;r1-az3-1", 2);
  }

  @Test
  public void testCreateTableSpaces_HappyPath_AllTsExist() {
    Universe universe =
        createFromConfig(defaultProvider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    mockFetchTablespaceAnswer("com/yugabyte/yw/controllers/tablespaces_shell_response2.txt");
    mockCreateTablespaceAnswer(ShellResponse.ERROR_CODE_SUCCESS, FETCH_TABLESPACE_ANSWER);
    verifyExecution(Success, universe.getUniverseUUID(), 3, "r1-az1-1;r1-az2-1;r1-az3-1", 1);
  }

  @Test
  public void testCreateTableSpaces_InvalidUniverse() {
    Universe universe = createUniverse("Universe", defaultCustomer.getId());

    mockCreateTablespaceAnswer(ShellResponse.ERROR_CODE_SUCCESS, FETCH_TABLESPACE_ANSWER);
    CreateTablespaceParams params =
        verifyExecution(Failure, universe.getUniverseUUID(), 3, "r1-az1-1;r1-az2-1;r1-az3-1", 0);
    verifyException(
        universe.getUniverseUUID(),
        params,
        "Cluster may not have been initialized yet. Please try later");
  }

  @Test
  public void testCreateTableSpaces_ErrorFetchingTablespaces() {
    Universe universe =
        createFromConfig(defaultProvider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    ShellResponse shellResponse =
        ShellResponse.create(ShellResponse.ERROR_CODE_GENERIC_ERROR, "Error");
    when(mockNodeUniverseManager.runYsqlCommand(
            any(), any(), any(), eq(CreateTableSpaces.FETCH_TABLESPACES_QUERY)))
        .thenReturn(shellResponse);

    CreateTablespaceParams params =
        verifyExecution(Failure, universe.getUniverseUUID(), 3, "r1-az1-1;r1-az2-1;r1-az3-1", 1);
    verifyException(
        universe.getUniverseUUID(),
        params,
        "Error while executing SQL request: Error occurred. Code: -1. Output: Error");
  }

  @Test
  public void testCreateTableSpaces_ErrorAnotherTablespaceExists() {
    Universe universe =
        createFromConfig(defaultProvider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    mockFetchTablespaceAnswer("com/yugabyte/yw/controllers/tablespaces_shell_response2.txt");
    CreateTablespaceParams params =
        verifyExecution(Failure, universe.getUniverseUUID(), 3, "r1-az1-1;r1-az2-1;r1-az4-1", 1);
    verifyException(
        universe.getUniverseUUID(),
        params,
        "Unable to create tablespace as another tablespace with"
            + " the same name 'test_tablespace' exists");
  }

  @Test
  public void testCreateTableSpaces_ErrorCreatingTablespaces() {
    Universe universe =
        createFromConfig(defaultProvider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    mockCreateTablespaceAnswer(ShellResponse.ERROR_CODE_GENERIC_ERROR, "Error");
    CreateTablespaceParams params =
        verifyExecution(Failure, universe.getUniverseUUID(), 3, "r1-az1-1;r1-az2-1;r1-az3-1", 2);
    verifyException(
        universe.getUniverseUUID(),
        params,
        "Error while executing SQL request: Error occurred. Code: -1. Output: Error");
  }

  private CreateTablespaceParams verifyExecution(
      State state, UUID universeUUID, int numReplicas, String config, int invocations) {
    CreateTablespaceParams params =
        generateTablespaceParams(universeUUID, defaultProvider.getCode(), numReplicas, config);
    TaskInfo taskInfo = submitTask(universeUUID, 1, params.tablespaceInfos);
    assertEquals(state, taskInfo.getTaskState());
    verify(mockNodeUniverseManager, times(invocations))
        .runYsqlCommand(any(), any(), any(), anyString());
    return params;
  }

  // The exception message is verified using direct call to the subtask.
  private void verifyException(UUID universeUUID, CreateTablespaceParams params, String errorMsg) {
    CreateTableSpaces.Params taskParams = new CreateTableSpaces.Params();
    taskParams.tablespaceInfos = params.tablespaceInfos;
    taskParams.expectedUniverseVersion = 1;
    taskParams.setUniverseUUID(universeUUID);

    CreateTableSpaces task =
        new CreateTableSpaces(mockBaseTaskDependencies, mockNodeUniverseManager);
    task.initialize(taskParams);
    RuntimeException re = assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(errorMsg, re.getMessage());
  }

  private void mockFetchTablespaceAnswer(String resourcename) {
    final String shellResponseString = TestUtils.readResource(resourcename);
    ShellResponse shellResponse =
        ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, shellResponseString);
    when(mockNodeUniverseManager.runYsqlCommand(
            any(), any(), any(), eq(CreateTableSpaces.FETCH_TABLESPACES_QUERY)))
        .thenReturn(shellResponse);
  }

  private void mockCreateTablespaceAnswer(int responseCode, String msg) {
    ShellResponse shellResponse = ShellResponse.create(responseCode, msg);
    when(mockNodeUniverseManager.runYsqlCommand(
            any(),
            any(),
            any(),
            AdditionalMatchers.not(eq(CreateTableSpaces.FETCH_TABLESPACES_QUERY))))
        .thenReturn(shellResponse);
  }

  private TaskInfo submitTask(
      UUID universeUUID, int version, List<TableSpaceInfo> tablespaceInfos) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    CreateTableSpaces.Params taskParams = new CreateTableSpaces.Params();
    taskParams.tablespaceInfos = tablespaceInfos;
    taskParams.expectedUniverseVersion = version;
    taskParams.setUniverseUUID(universe.getUniverseUUID());

    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateTableSpaces, taskParams);
      // Set http context
      Users defaultUser = ModelFactory.testUser(defaultCustomer);
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          universe.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.CreateTableSpaces,
          universe.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }
}
