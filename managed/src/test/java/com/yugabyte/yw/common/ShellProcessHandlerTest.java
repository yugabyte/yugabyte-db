// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ShellProcessHandler.YB_LOGS_MAX_MSG_SIZE;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import junit.framework.TestCase;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ShellProcessHandlerTest extends TestCase {
  @InjectMocks ShellProcessHandler shellProcessHandler;

  @Mock play.Configuration appConfig;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  @Mock Config mockConfig;

  static String TMP_STORAGE_PATH = "/tmp/yugaware_tests/spht_certs";
  static final String COMMAND_OUTPUT_LOGS_DELETE = "yb.logs.cmdOutputDelete";

  @Before
  public void beforeTest() {
    new File(TMP_STORAGE_PATH).mkdirs();
    when(appConfig.getString("yb.devops.home")).thenReturn(TMP_STORAGE_PATH);
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);
    when(mockConfig.getBoolean(COMMAND_OUTPUT_LOGS_DELETE)).thenReturn(true);
    when(appConfig.getBytes(YB_LOGS_MAX_MSG_SIZE)).thenReturn(2000L);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  @Test
  public void testRunWithValidCommandAndDevopsHome() {
    List<String> command = new ArrayList<String>();
    command.add("pwd");
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(0, response.code);
    assertThat(response.message, allOf(notNullValue(), containsString(TMP_STORAGE_PATH)));
    assertEquals(response.message.trim(), response.message);
  }

  @Test
  public void testPartialLineOutput() throws IOException {
    List<String> command = new ArrayList<String>();
    String partialLineCmd = "printf foo && sleep 1 && printf bar";
    String fileName = createTestShellScript(partialLineCmd);
    command.add(fileName);
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(0, response.code);
    assertEquals(response.message.trim(), "foobar");
  }

  @Test
  public void testRunWithInvalidDevopsHome() {
    when(appConfig.getString("yb.devops.home")).thenReturn("/foo");
    List<String> command = new ArrayList<String>();
    command.add("pwd");
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(-1, response.code);
    assertThat(
        response.message,
        allOf(
            notNullValue(),
            equalTo(
                "Cannot run program \"pwd\" (in directory \"/foo\"): "
                    + "error=2, No such file or directory")));
  }

  @Test
  public void testRunWithInvalidCommand() throws IOException {
    String testCmd = ">&2 echo error; sleep 2; echo foobar; sleep 2; echo more; exit 255";
    String fileName = createTestShellScript(testCmd);
    List<String> command = new ArrayList<String>();
    command.add(fileName);
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(255, response.code);
    assertThat(response.message.trim(), allOf(notNullValue(), equalTo("error")));
  }

  private String createTestShellScript(String cmd) throws IOException {
    Path fileName = Files.createTempFile(Paths.get(TMP_STORAGE_PATH), "yw_test", ".sh");
    Files.write(fileName, ("#/bin/bash\n" + cmd).getBytes());
    fileName.toFile().setExecutable(true);
    return fileName.toString();
  }
}
