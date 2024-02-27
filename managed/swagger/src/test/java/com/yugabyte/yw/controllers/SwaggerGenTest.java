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
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.swagger.PlatformModelConverter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import io.swagger.jackson.mixin.ResponseSchemaMixin;
import io.swagger.models.Response;
import io.swagger.util.ReferenceSerializationConfigurer;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Map.Entry;
import java.util.stream.Stream;
import org.apache.commons.io.IOUtils;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.routing.Router;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.modules.swagger.SwaggerModule;
import play.mvc.Result;
import play.test.Helpers;

/**
 * Unittest can be run like this: [yugaware] $ testOnly com.yugabyte.yw.controllers.SwaggerGenTest
 * This test will fail if swagger test has not been generated.
 *
 * <p>Run following command to update the spec so that the test passes: [yugaware] swaggerGen
 */
public class SwaggerGenTest extends FakeDBApplication {
  private static final Logger LOG = LoggerFactory.getLogger(SwaggerGenTest.class);

  @Override
  protected boolean isSwaggerEnabled() {
    return true;
  }

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    return super.configureApplication(
        builder
            .overrides(new SwaggerModule())
            .overrides(bind(Router.class).toProvider(SwaggerGenRoutesProvider.class)));
  }

  @Test
  public void genJson() throws IOException, NoSuchFieldException, IllegalAccessException {
    String resourceName = "swagger.json";
    System.err.println(app.classloader().getResource(resourceName).getFile());
    try (InputStream is = app.classloader().getResourceAsStream(resourceName)) {
      String expectedSwagger = IOUtils.toString(is, StandardCharsets.UTF_8);
      String actualSwaggerSpec = getSwaggerSpec();
      if (actualSwaggerSpec.length() != expectedSwagger.length()) {
        // TODO: Fix this: Only length comparison because the json ordering change
        // There still may be some ordering changes depending on machine architecture since
        // the json still has few lists that are not sorted. But most likely this is a non-issue.
        File outFile = File.createTempFile("swagger", ".json");
        try (FileWriter fileWriter = new FileWriter(outFile)) {
          fileWriter.write(actualSwaggerSpec);
        }
        fail(
            "==============================================================================\n"
                + "Run $sbt swaggerGen\n  The new swagger spec written to:\n"
                + outFile.getAbsolutePath()
                + "\n==========================================================================\n");
      }
    }
  }

  @Test
  public void genMd() throws IOException {
    String expectedFlags = expectedFlags();
    String currentFlags = currentFlags();
    if (!expectedFlags.equals(currentFlags)) {
      fail(
          "==============================================================================\n"
              + "Flags defination changed.Run $sbt swaggenGen\n"
              + "\n==========================================================================\n");
    }
  }

  // Any newly introduced date type of field in the YBA API should be in the RFC3339 format.
  // This unit test fails if there this is not conformed to.
  @Test
  public void checkDateFormats()
      throws JsonProcessingException, NoSuchFieldException, IllegalAccessException {
    String actualSwaggerSpec = getSwaggerSpec();
    JsonNode root = Json.parse(actualSwaggerSpec);
    JsonNode defs = root.get("definitions");
    Map<String, String> result = checkDate("", defs);
    if (!result.isEmpty()) {
      String resultFormatted =
          dumpFields(result)
              + "\n\n"
              + "=========================================================\n"
              + "Any date-time type of field in the API should be set in RFC3339 format for the\n"
              + "go-client to be able to deserialize it. So the annotations for such a type"
              + " should\n"
              + "look like this.\n"
              + "\n"
              + "@Column(nullable = false)\n"
              + "@ApiModelProperty(\n"
              + "    value = \"Creation date of key\",\n"
              + "    required = false,\n"
              + "    example = \"2022-12-12T13:07:18Z\",\n"
              + "    accessMode = AccessMode.READ_ONLY)\n"
              + "@JsonFormat(shape = JsonFormat.Shape.STRING, pattern ="
              + " \"yyyy-MM-dd'T'HH:mm:ss'Z'\")\n"
              + "@Getter\n"
              + "public Date creationDate;\n"
              + "=========================================================\n\n";
      fail("Error in date-time formats for the fields\n" + resultFormatted);
    }
  }

  private String dumpFields(Map<String, String> fields) {
    String result = fields.size() + " errors\n";
    for (Entry<String, String> entry : fields.entrySet()) {
      result = result.concat(entry.getKey() + ":\t");
      result = result.concat(entry.getValue() + "\n");
    }
    return result;
  }

  // returns a map of swagger entry keys that have an issue with it date-time
  // formatting along with corresponding error message.
  private Map<String, String> checkDate(String name, JsonNode node) {
    // Handle 3 types of JsonNode:
    // Object node: check if object has a "format: date-time" field.
    // If so, it should also have "example: 2022-12-12T13:07:18Z".
    // if no errors, continue processing nested objects.
    // Value node: nothing to process, skip
    // Array node: process each node in turn
    if (node.isArray()) {
      for (JsonNode item : node) {
        Map<String, String> result = checkDate(name, item);
        if (!result.isEmpty()) {
          Map<String, String> prependedKeys = Maps.newHashMap();
          result.forEach((k, v) -> prependedKeys.put(name + "." + k, v));
          return prependedKeys;
        }
      }
    } else if (node.isObject()) {
      LOG.trace("checking date format in " + name);
      String err = checkDateInNode(name, node);
      if (!err.isEmpty()) {
        // if this JsonNode object has errors, don't look inside nested structures
        return Collections.singletonMap(name, err);
      }
      // Since this JsonNode object has no errors, look into nested objects
      // and collect all errors into allResults.
      Map<String, String> allResults = Maps.newHashMap();
      Iterator<Entry<String, JsonNode>> fields = node.fields();
      while (fields.hasNext()) {
        Entry<String, JsonNode> entry = fields.next();
        if (entry.getValue().isValueNode()) {
          continue;
        }
        Map<String, String> result = checkDate(entry.getKey(), entry.getValue());
        if (result != null) {
          // prepend current node name in returned key name
          Map<String, String> prependedKeys = Maps.newHashMap();
          result.forEach((k, v) -> prependedKeys.put(name + "." + k, v));
          allResults.putAll(prependedKeys);
        }
      }
      return allResults;
    }

    return Maps.newHashMap();
  }

  // checks for date format in the given JsonNode that is a Json Object,
  // and returns the error message.
  private String checkDateInNode(String name, JsonNode node) {
    if (node.has("description")
        && node.get("description").textValue() != null
        && node.get("description").textValue().contains("Deprecated")) {
      // skip check if the field is deprecated
      return "";
    }
    if (node.has("format")
        && node.get("format").isTextual()
        && node.get("format").textValue().equals("date-time")) {
      // if node has "format: date-time", then it should also have "example"
      if (!node.has("example")) {
        String err = name + " is a date-time field but does not have example";
        return err;
        // fail("Found node " + name + " with contents " + node.toString() + " with
        // date-time but without example");
      } else {
        String valueInAPIExample = node.get("example").textValue();
        try {
          new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US).parse(valueInAPIExample);
        } catch (ParseException e) {
          LOG.warn(e.getMessage());
          String err =
              name
                  + " has an example "
                  + valueInAPIExample
                  + ". It has be to RFC3339 parsable in the form 2022-12-12T13:07:18Z";
          return err;
        }
      }
    }
    return "";
  }

  private String getSwaggerSpec()
      throws NoSuchFieldException, IllegalAccessException, JsonProcessingException {
    // So, this one is really tricky. The only play2.8 compatible swagger library I've found,
    // with the same functionality as our previous library, is
    // https://github.com/dwickern/swagger-play. It works fine, except that readOnly fields are
    // not serialized in the resulting swagger.json.
    // The reason is in this "workaround" for the issue with readOnly field they faced:
    // https://github.com/dwickern/swagger-play/commit/b04e4a2e145a277592d29c09d482ccb92138fa14#diff-a74181fef5ee291b8ecaf4773e3e4df1e335e0f0f3640b51f60298e00a143ba2
    // Basically, in marks AbstractProperty.readOnly() method with @JsonIgnore annotation.
    // At the same time - jackson-module-scala adds ScalaAnnotationIntrospector
    // to Swagger ObjectMapper, which makes it treat readOnly() method as getter for readOnly field.
    // As a result - jackson finds all getters/setters for readOnly field across the whole type
    // hierarchy, and treats this field as @JsonIgnore as a result of that.
    //
    // Tried to undo the above "workaround" - but this really causes InvalidDefinitionException
    // during serialization. Decided to not dig into it further to find a better fix for this
    // serialization cycle issue and instead replaced Swagger ObjectMapper with our default mapper
    // with ReferenceSerializationConfigurer.serializeAsComputedRef and ResponseSchemaMixin,
    // which swagger itself adds by default (see ObjectMapperFactory).
    ObjectMapper mapper = CustomObjectMapperModule.createDefaultMapper();
    mapper.addMixIn(Response.class, ResponseSchemaMixin.class);
    ReferenceSerializationConfigurer.serializeAsComputedRef(mapper);
    Field mapperField = io.swagger.util.Json.class.getDeclaredField("mapper");
    mapperField.setAccessible(true);
    mapperField.set(null, mapper);

    Result result = route(Helpers.fakeRequest("GET", "/docs/dynamic_swagger.json"));
    return sort(contentAsString(result, getApp().asScala().materializer()));
  }

  private String expectedFlags() {
    Customer defaultCustomer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(defaultCustomer, Users.Role.SuperAdmin);
    String authToken = user.createAuthToken();
    Result result =
        doRequestWithAuthToken("GET", "/api/runtime_config/mutable_key_info", authToken);
    String res = "# <u>List of supported Runtime Configuration Flags</u>\n";
    res += "### These are all the public runtime flags in YBA.\n";
    res += "| Name | Key | Scope | Help Text | Data Type |\n";
    res += "| :----: | :----: | :----: | :----: | :----: |\n";
    for (JsonNode j : Json.parse(contentAsString(result))) {
      if (!j.get("tags").toString().contains("PUBLIC")) continue;
      res += "| " + j.get("displayName") + " | ";
      res += j.get("key") + " | ";
      res += j.get("scope") + " | ";
      res += j.get("helpTxt") + " | ";
      res += j.get("dataType").get("name") + " |\n";
    }
    return res;
  }

  private String currentFlags() {
    Path p = Paths.get("").toAbsolutePath().getParent().resolve("RUNTIME-FLAGS.md");
    StringBuilder contentBuilder = new StringBuilder();
    try (Stream<String> stream = Files.lines(p, StandardCharsets.UTF_8)) {
      stream.forEach(S -> contentBuilder.append(S).append("\n"));
    } catch (Exception e) {
      fail("failed to fetch file. path= " + p.toString());
    }
    return contentBuilder.toString();
  }

  // we will deserialize and serialize back using this mapper so as to generate deterministic
  // sorted diffable/mergable json:
  private static final ObjectMapper SORTED_MAPPER = Json.mapper();

  static {
    SORTED_MAPPER.configure(SerializationFeature.INDENT_OUTPUT, true);
    SORTED_MAPPER.configure(MapperFeature.SORT_PROPERTIES_ALPHABETICALLY, true);
    SORTED_MAPPER.configure(SerializationFeature.ORDER_MAP_ENTRIES_BY_KEYS, true);
  }

  private String sort(String jsonString) throws JsonProcessingException {
    ObjectNode specJsonNode = (ObjectNode) Json.parse(jsonString);
    deleteApiUrl(specJsonNode);
    sortTagsList(specJsonNode);
    final Object obj = SORTED_MAPPER.treeToValue(specJsonNode, Object.class);
    return SORTED_MAPPER.writeValueAsString(obj);
  }

  // Do not hardcode API url
  private void deleteApiUrl(ObjectNode specJsonNode) {
    specJsonNode.remove("host");
    specJsonNode.remove("basePath");
  }

  // This way swagger UI will show tags in sorted order.
  private void sortTagsList(ObjectNode specJsonNode) {
    ArrayNode tagsArrayNode = (ArrayNode) specJsonNode.get("tags");
    List<JsonNode> tagsList = Lists.newArrayList(tagsArrayNode.elements());
    tagsList.sort(Comparator.comparing(tagObject -> tagObject.get("name").asText()));
    ArrayNode sortedTagsArrayNode = Json.newArray().addAll(tagsList);
    specJsonNode.set("tags", sortedTagsArrayNode);
  }

  public static void main(String[] args)
      throws IOException, NoSuchFieldException, IllegalAccessException {
    String expectedSwagger = getCurrentSpec(args[0]);
    excludeDeprecated(args);
    SwaggerGenTest swaggerGenTest = new SwaggerGenTest();
    try {
      swaggerGenTest.startServer();
      String expectedFlags = swaggerGenTest.expectedFlags();
      String actualFlags = swaggerGenTest.currentFlags();
      if (!actualFlags.equals(expectedFlags)) {
        Path p = Paths.get("").toAbsolutePath().getParent().resolve("RUNTIME-FLAGS.md");
        try (FileWriter fw = new FileWriter(p.toFile())) {
          fw.write(expectedFlags);
        }
        System.out.println("New flags doc generated...");
      } else {
        System.out.println("Runtime flags haven't changed");
      }
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
      swaggerGenTest.stopServer();
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

  private static void excludeDeprecated(String[] args) {
    PlatformModelConverter.excludeYbaDeprecatedOption = "";
    if (args.length > 2 && args[1].equalsIgnoreCase("--exclude_deprecated")) {
      PlatformModelConverter.excludeYbaDeprecatedOption = args[2];
    }
  }
}
