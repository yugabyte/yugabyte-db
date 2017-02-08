// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class ShellProcessHandlerTest {
    @InjectMocks
    ShellProcessHandler shellProcessHandler;

    @Mock
    play.Configuration appConfig;
    static String YB_DEVOPS_HOME = "/tmp/yugaware_tests";

    @BeforeClass
    public static void setUp() {
        new File(YB_DEVOPS_HOME).mkdir();
    }

    @AfterClass
    public static void tearDown() {
        File file = new File(YB_DEVOPS_HOME);
        file.delete();
    }


    @Before
    public void beforeTest() {
        when(appConfig.getString("yb.devops.home")).thenReturn(YB_DEVOPS_HOME);
    }

    @Test
    public void testRunWithValidCommandAndDevopsHome() {
        List<String> command = new ArrayList<String>();
        command.add("pwd");
        ShellProcessHandler.ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
        assertEquals(0, response.code);
        assertThat(response.message, allOf(notNullValue(), containsString(YB_DEVOPS_HOME)));
    }

    @Test
    public void testRunWithInvalidDevopsHome() {
        when(appConfig.getString("yb.devops.home")).thenReturn("/foo");
        List<String> command = new ArrayList<String>();
        command.add("pwd");
        ShellProcessHandler.ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
        assertEquals(-1, response.code);
        assertThat(response.message, allOf(notNullValue(),
                equalTo("Cannot run program \"pwd\" (in directory \"/foo\"): " +
                        "error=2, No such file or directory")));
    }

    @Test
    public void testRunWithInvalidCommand() throws IOException {
        String fileName = createTestShellScript();
        List<String> command = new ArrayList<String>();
        command.add(fileName);
        ShellProcessHandler.ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
        assertEquals(255, response.code);
        assertThat(response.message, allOf(notNullValue(), equalTo("error")));
    }

    private String createTestShellScript() throws IOException {
        String fileName = YB_DEVOPS_HOME + "/test.sh";
        FileWriter fw = new FileWriter(fileName);
        fw.write(">&2 echo \"error\"\nexit -1");
        fw.close();
        // Set the file as a executable
        File file = new File(fileName);
        file.setExecutable(true);
        return fileName;
    }
}
