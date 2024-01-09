/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.google.common.base.Strings;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiOperation;
import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.lang.annotation.Annotation;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.PriorityQueue;
import java.util.Queue;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

/**
 * This is not a regular unit test. This is a validitiy checker tool for the @YbaApi annotation.
 * This runs as a unit test and helps developers with making changes to the YBA API. It ensures that
 * the API visibility intent specified in @YbaApi is reflected correctly through swagger's
 * annotations and values.
 *
 * <p>This unit test needs to load all the API classes for reflection. That's the reason it is part
 * of main java unit test rather than a swagger project unit tests.
 */
public class YbaApiTest extends FakeDBApplication {
  private static Map<String, Set<Method>> allApiMethods = new HashMap<>();
  private static Map<String, Set<Field>> allApiFields = new HashMap<>();
  private static Map<String, Set<Method>> ybaApiAnnotatedMethods = new HashMap<>();
  private static Map<String, Set<Field>> ybaApiAnnotatedFields = new HashMap<>();

  // description message enforced for each visibility level
  private static String DEPRECATION_MESSAGE = "Deprecated since YBA version %s";
  private static String INTERNAL_MESSAGE = "YbaApi Internal";
  private static String PREVIEW_MESSAGE = "WARNING: This is a preview API that could change";
  private static String PUBLIC_MESSAGE = "Available since YBA version %s";
  // part of description message to identify a visibility level
  private static String DEPRECATION_MESSAGE_PART = "deprecated";
  private static String INTERNAL_MESSAGE_PART_1 = "internal";
  private static String INTERNAL_MESSAGE_PART_2 = "ybm";
  private static String PREVIEW_MESSAGE_PART = "preview API";

  @BeforeClass
  public static void collectApiClasses() {
    // Scan all classes under com.yugabyte.yw
    Queue<String> packagesToScan = new PriorityQueue<>(Arrays.asList("com.yugabyte.yw"));
    while (!packagesToScan.isEmpty()) {
      String packageName = packagesToScan.remove();
      Set<String> nestedPackages = getNestedPackageNames(packageName);
      if (nestedPackages.size() > 0) {
        System.err.println("Adding nested packages to scan: " + nestedPackages);
        packagesToScan.addAll(nestedPackages);
      }

      for (String apiClassName : getClassNames(packageName)) {
        Set<Method> apiMethods = collectAnnotatedMethods(apiClassName, ApiOperation.class);
        if (apiMethods.size() > 0) {
          allApiMethods.put(apiClassName, apiMethods);
        }
        Set<Field> apiFields = collectAnnotatedFields(apiClassName, ApiModelProperty.class);
        if (apiFields.size() > 0) {
          allApiFields.put(apiClassName, apiFields);
        }
        Set<Method> ybaApiMethods = collectAnnotatedMethods(apiClassName, YbaApi.class);
        if (ybaApiMethods.size() > 0) {
          ybaApiAnnotatedMethods.put(apiClassName, ybaApiMethods);
        }
        Set<Field> ybaApiFields = collectAnnotatedFields(apiClassName, YbaApi.class);
        if (ybaApiFields.size() > 0) {
          ybaApiAnnotatedFields.put(apiClassName, ybaApiFields);
        }
      }
    }
    System.err.println(ybaApiAnnotatedMethods.size() + " YbaApi annotated methods found");
    System.err.println(ybaApiAnnotatedFields.size() + " YbaApi annotated fields found");
  }

  private static BufferedReader getBufferedReader(String packageName) {
    InputStream stream =
        ClassLoader.getSystemClassLoader().getResourceAsStream(packageName.replaceAll("[.]", "/"));
    return new BufferedReader(new InputStreamReader(stream));
  }

  private static Set<String> getClassNames(String packageName) {
    return getBufferedReader(packageName)
        .lines()
        .filter(line -> line.endsWith(".class"))
        .map(dotClass -> packageName + "." + dotClass.substring(0, dotClass.lastIndexOf('.')))
        .collect(Collectors.toSet());
  }

  private static Set<String> getNestedPackageNames(String packageName) {
    return getBufferedReader(packageName)
        .lines()
        .filter(line -> !line.endsWith(".class"))
        .map(p -> packageName + "." + p)
        .collect(Collectors.toSet());
  }

  private static Set<Method> collectAnnotatedMethods(
      String apiClassName, Class<? extends Annotation> annToLookFor) {
    try {
      return Arrays.stream(Class.forName(apiClassName).getDeclaredMethods())
          .filter(method -> method.isAnnotationPresent(annToLookFor))
          .collect(Collectors.toSet());
    } catch (ClassNotFoundException e) {
      return Collections.emptySet();
    }
  }

  private static Set<Field> collectAnnotatedFields(
      String apiClassName, Class<? extends Annotation> annToLookFor) {
    try {
      return Arrays.stream(Class.forName(apiClassName).getDeclaredFields())
          .filter(field -> field.isAnnotationPresent(annToLookFor))
          .collect(Collectors.toSet());
    } catch (ClassNotFoundException e) {
      return Collections.emptySet();
    }
  }

  private String expectedVisibilityMessage(YbaApi ybaApiAnn) {
    switch (ybaApiAnn.visibility()) {
      case DEPRECATED:
        return String.format(DEPRECATION_MESSAGE, ybaApiAnn.sinceYBAVersion());
      case INTERNAL:
        return INTERNAL_MESSAGE;
      case PREVIEW:
        return PREVIEW_MESSAGE;
      case PUBLIC:
        return String.format(PUBLIC_MESSAGE, ybaApiAnn.sinceYBAVersion());
      default:
        // Cannot come here, since setting the visibility is mandatory in a YbaApi
        // annotation
        Assert.fail("Setting the visibility is mandatory in a YbaApi annotation");
        return null;
    }
  }

  private void verifyRuntimeConfig(YbaApi ybaApiAnn) {
    if (Strings.isNullOrEmpty(ybaApiAnn.runtimeConfig())) {
      return;
    }
    // TODO: Load the GlobalConfKeys and validate it has a boolean flag of this name
  }

  @Test
  public void verifyYbaApiAnnotatedMethods() {
    for (String className : ybaApiAnnotatedMethods.keySet()) {
      Set<Method> ybaApiMethods = ybaApiAnnotatedMethods.get(className);
      for (Method m : ybaApiMethods) {
        String errMsg = "For " + className + "." + m.getName() + "(): ";
        // A method having @YbaApi should also have @ApiOperation
        YbaApi ybaApiAnn = m.getAnnotation(YbaApi.class);
        Assert.assertNotNull(errMsg, ybaApiAnn);
        boolean isGetterSetter = m.getName().startsWith("get") || m.getName().startsWith("set");
        String annDescription = null;
        ApiOperation apiOpAnn = m.getAnnotation(ApiOperation.class);
        if (apiOpAnn != null) {
          annDescription = apiOpAnn.value();
        } else if (isGetterSetter) {
          ApiModelProperty apiPropAnn = m.getAnnotation(ApiModelProperty.class);
          Assert.assertNotNull(
              errMsg + "Add corresponding @ApiModelProperty annotation", apiPropAnn);
          annDescription = apiPropAnn.value();
        } else {
          Assert.fail(errMsg + "Add corresponding @ApiOperation annotation");
        }
        // @Deprecated should exist only for visibility=DEPRECATED
        if (!isGetterSetter && ybaApiAnn.visibility().equals(YbaApiVisibility.DEPRECATED)) {
          Assert.assertNotNull(
              errMsg + "Should have @Deprecated annotation", m.getAnnotation(Deprecated.class));
        } else {
          Assert.assertFalse(
              "@Deprecated can be added only along with @YbaApi(visbility=DEPRECATED)",
              m.isAnnotationPresent(Deprecated.class));
        }
        // @ApiOperation should have a message corresponding to the visibility of @YbaApi
        String expectedVisibilityMsg = expectedVisibilityMessage(ybaApiAnn);
        errMsg +=
            "Please update corresponding "
                + (isGetterSetter ? "@ApiModelProperty(value = \"" : "@ApiOperation(value = \"")
                + expectedVisibilityMsg
                + ". <...anything else here...>\")";
        Assert.assertNotNull(errMsg, annDescription);
        Assert.assertTrue(errMsg, annDescription.contains(expectedVisibilityMsg));
        // validate the runtimeConfig flag of @YbaApi if present
        verifyRuntimeConfig(ybaApiAnn);

        // TODO: validate the params
      }
    }
  }

  @Test
  public void verifyYbaApiAnnotatedFields() {
    for (String className : ybaApiAnnotatedFields.keySet()) {
      Set<Field> ybaApiFields = ybaApiAnnotatedFields.get(className);
      for (Field f : ybaApiFields) {
        String errMsg = "For " + className + "." + f.getName() + ": ";
        // A field having @YbaApi should also have @ApiModelProperty
        YbaApi ybaApiAnn = f.getAnnotation(YbaApi.class);
        Assert.assertNotNull(errMsg, ybaApiAnn);
        ApiModelProperty apiPropAnn = f.getAnnotation(ApiModelProperty.class);
        Assert.assertNotNull(errMsg + "Add corresponding @ApiModelProperty annotation", apiPropAnn);
        // @ApiModelProperty should have a message corresponding to the visibility of @YbaApi
        String expectedVisibilityMsg = expectedVisibilityMessage(ybaApiAnn);
        errMsg +=
            "Please update corresponding @ApiModelProperty(value = \""
                + expectedVisibilityMsg
                + ". <...anything else here...>\")";
        Assert.assertNotNull(errMsg, apiPropAnn.value());
        Assert.assertTrue(errMsg, apiPropAnn.value().contains(expectedVisibilityMsg));
        // validate the runtimeConfig flag of @YbaApi if present
        verifyRuntimeConfig(ybaApiAnn);
      }
    }
  }

  // returns the expected visibility level of YbaApi by looking for keywords in the given API
  // description
  private YbaApiVisibility expectedVisibility(String apiDescription) {
    // A method having "deprecated" in its value should also have
    // @YbaApi(visibility=DEPRECATED)
    if (apiDescription.contains(DEPRECATION_MESSAGE_PART)) {
      return YbaApiVisibility.DEPRECATED;
    }
    // A method having "internal"/"ybm" in its value should also have
    // @YbaApi(visibility=INTERNAL)
    if (apiDescription.contains(INTERNAL_MESSAGE_PART_1)
        || apiDescription.contains(INTERNAL_MESSAGE_PART_2)) {
      return YbaApiVisibility.INTERNAL;
    }
    // A method having "preview API" in its value should also have
    // @YbaApi(visibility=PREVIEW)
    if (apiDescription.contains(PREVIEW_MESSAGE_PART)) {
      return YbaApiVisibility.PREVIEW;
    }
    return null;
  }

  @Test
  public void verifyApiOperationMethods() {
    for (String className : allApiMethods.keySet()) {
      Set<Method> apiMethods = allApiMethods.get(className);
      for (Method m : apiMethods) {
        String errMsg = "For " + className + "." + m.getName() + "(): ";
        ApiOperation apiOpAnn = m.getAnnotation(ApiOperation.class);
        YbaApiVisibility expectedVisibility = expectedVisibility(apiOpAnn.value());
        if (expectedVisibility != null) {
          errMsg += "Expected @YbaApi(visibility=" + expectedVisibility.name() + ")";
          YbaApi ybaApi = m.getAnnotation(YbaApi.class);
          Assert.assertNotNull(errMsg, ybaApi);
          Assert.assertTrue(errMsg, ybaApi.visibility().equals(expectedVisibility));
        }
      }
    }
  }

  @Test
  public void verifyApiModelPropertyFields() {
    for (String className : allApiFields.keySet()) {
      Set<Field> apiFields = allApiFields.get(className);
      for (Field f : apiFields) {
        String errMsg = "For " + className + "." + f.getName() + ": ";
        ApiModelProperty apiPropAnn = f.getAnnotation(ApiModelProperty.class);
        Assert.assertNotNull(errMsg + "Add corresponding @ApiModelProperty annotation", apiPropAnn);
        YbaApiVisibility expectedVisibility = expectedVisibility(apiPropAnn.value());
        if (expectedVisibility != null) {
          errMsg += "Expected @YbaApi(visibility=" + expectedVisibility.name() + ")";
          YbaApi ybaApiAnn = f.getAnnotation(YbaApi.class);
          Assert.assertNotNull(errMsg, ybaApiAnn);
          Assert.assertTrue(errMsg, ybaApiAnn.visibility().equals(expectedVisibility));
        }
      }
    }
  }
}
