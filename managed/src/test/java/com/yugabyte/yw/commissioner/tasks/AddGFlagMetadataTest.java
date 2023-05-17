// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AddGFlagMetadataTest extends CommissionerBaseTest {

  private TaskInfo submitTask(TaskType taskType, ITaskParams taskParams, boolean isSuccess) {
    TaskInfo taskInfo = null;
    try {
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      waitForTask(taskUUID);
      taskInfo = TaskInfo.getOrBadRequest(taskUUID);
      if (isSuccess) {
        assertEquals(Success, taskInfo.getTaskState());
      } else {
        assertEquals(Failure, taskInfo.getTaskState());
      }
    } catch (Exception e) {
      assertNull(e.getMessage());
    }
    return taskInfo;
  }

  private ReleaseMetadata defaultReleaseMetadata;
  private final String DEFAULT_VERSION = "1.0.0.0-b1";
  private String tempFilePath;

  @Before
  public void setup() {
    super.setUp();
    tempFilePath =
        TestHelper.createTempFile(TestHelper.TMP_PATH, DEFAULT_VERSION + ".tar.gz", "test-file");
    defaultReleaseMetadata = ReleaseMetadata.create(DEFAULT_VERSION).withFilePath(tempFilePath);
  }

  @Test
  public void testAddGFlagMetadataSuccess() throws FileNotFoundException, Exception {
    AddGFlagMetadata.Params params = new AddGFlagMetadata.Params();
    params.requiredGFlagsFileList = GFlagsValidation.GFLAG_FILENAME_LIST;
    params.releaseMetadata = defaultReleaseMetadata;
    params.version = DEFAULT_VERSION;
    when(mockReleaseManager.getTarGZipDBPackageInputStream(any(), any()))
        .thenReturn(new FileInputStream(tempFilePath));
    doNothing()
        .when(mockGFlagsValidation)
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());
    TaskInfo taskInfo = submitTask(TaskType.AddGFlagMetadata, params, true);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testFailureWhileFetchingInputStream() throws FileNotFoundException, Exception {
    AddGFlagMetadata.Params params = new AddGFlagMetadata.Params();
    params.requiredGFlagsFileList = GFlagsValidation.GFLAG_FILENAME_LIST;
    params.releaseMetadata = defaultReleaseMetadata;
    params.version = DEFAULT_VERSION;
    when(mockReleaseManager.getTarGZipDBPackageInputStream(any(), any()))
        .thenThrow(new RuntimeException("File does not exists"));
    TaskInfo taskInfo = submitTask(TaskType.AddGFlagMetadata, params, false);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testFailureWhileWritingFlagFile() throws FileNotFoundException, Exception {
    AddGFlagMetadata.Params params = new AddGFlagMetadata.Params();
    params.requiredGFlagsFileList = GFlagsValidation.GFLAG_FILENAME_LIST;
    params.releaseMetadata = defaultReleaseMetadata;
    params.version = DEFAULT_VERSION;
    when(mockReleaseManager.getTarGZipDBPackageInputStream(any(), any()))
        .thenReturn(new FileInputStream(tempFilePath));
    doThrow(new IOException("Error-Message"))
        .when(mockGFlagsValidation)
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());
    TaskInfo taskInfo = submitTask(TaskType.AddGFlagMetadata, params, false);
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
