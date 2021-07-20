/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static junit.framework.TestCase.fail;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.route;

import com.yugabyte.yw.common.FakeDBApplication;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import org.apache.commons.io.IOUtils;
import org.junit.Test;
import play.mvc.Result;
import play.test.Helpers;

/**
 * Unittest can be run like this: [yugaware] $ testOnly com.yugabyte.yw.controllers.SwaggerGenTest
 * This test will fail if swagger test has not been generated.
 *
 * <p>Run following command to update the spec so that the test passes: [yugaware] swaggerGen
 */
public class SwaggerGenTest extends FakeDBApplication {

  @Override
  protected boolean isSwaggerEnabled() {
    return true;
  }

  @Test
  public void genJson() throws IOException {
    String resourceName = "swagger.json";
    System.err.println(app.classloader().getResource(resourceName).getFile());
    try (InputStream is = app.classloader().getResourceAsStream(resourceName)) {
      String expectedSwagger = IOUtils.toString(is, StandardCharsets.UTF_8);
      String actualSwaggerSpec = getSwaggerSpec();
      if (actualSwaggerSpec.length() != expectedSwagger.length()) {
        // TODO: Fix this: Only length comparison because the json ordering change
        File outFile = File.createTempFile("swagger", ".json");
        try (FileWriter fileWriter = new FileWriter(outFile)) {
          fileWriter.write(actualSwaggerSpec);
        }
        fail(
            "=============================================================================="
                + "Run $sbt swaggerGen\n  The new swagger spec written to:"
                + outFile.getAbsolutePath()
                + "==============================================================================");
      }
    }
  }

  private String getSwaggerSpec() {
    Result result = route(Helpers.fakeRequest("GET", "/docs/swagger.json"));
    return contentAsString(result, mat);
  }

  public static void main(String[] args) throws IOException {
    String expectedSwagger = getCurrentSpec(args[0]);
    SwaggerGenTest swaggerGenTest = new SwaggerGenTest();
    try {
      swaggerGenTest.startPlay();
      System.out.println("Generating swagger spec:" + Arrays.toString(args));
      String swaggerSpec = swaggerGenTest.getSwaggerSpec();
      if (expectedSwagger.length() == swaggerSpec.length()) {
        // TODO: Fix this: Only length comparison because the json ordering change
        System.out.println("Swagger Specs have not changed");
        return;
      }
      try (FileWriter fileWriter = new FileWriter(new File(args[0]))) {
        // todo only generate on change
        fileWriter.write(swaggerSpec);
        System.out.println("Swagger spec generated");
      }
    } finally {
      swaggerGenTest.stopPlay();
    }
  }

  private static String getCurrentSpec(String arg) {
    String expectedSwagger;
    try (InputStream is = new FileInputStream(arg)) {
      expectedSwagger = IOUtils.toString(is, StandardCharsets.UTF_8);
    } catch (Exception exception) {
      throw new RuntimeException(exception);
    }
    return expectedSwagger;
  }
}
