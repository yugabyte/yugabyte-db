// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.xml.JacksonXmlModule;
import com.fasterxml.jackson.dataformat.xml.XmlMapper;
import com.fasterxml.jackson.dataformat.xml.annotation.JacksonXmlElementWrapper;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Singleton;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;

@Singleton
public class GFlagsValidation {

  private final Environment environment;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidation.class);

  @Inject
  public GFlagsValidation(Environment environment) {
    this.environment = environment;
  }

  public List<GFlagDetails> extractGFlags(String version, String serverType, boolean mostUsedGFlags)
      throws IOException {
    InputStream flagStream =
        environment.resourceAsStream(
            "gflags_metadata/" + version + "/" + serverType.toLowerCase() + ".xml");
    // If the metadata of the given db version (eg 2.9.2.0-b78) is present then we can take up the
    // major version(2.9)
    // metadata
    if (flagStream == null) {
      String majorVersion = version.substring(0, StringUtils.ordinalIndexOf(version, ".", 2));
      flagStream =
          environment.resourceAsStream(
              "gflags_metadata/" + majorVersion + "/" + serverType.toLowerCase() + ".xml");
      if (flagStream == null) {
        LOG.error("GFlags metadata file for " + majorVersion + " is not present");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "GFlags metadata file for " + majorVersion + " is not present");
      }
    }
    JacksonXmlModule xmlModule = new JacksonXmlModule();
    xmlModule.setDefaultUseWrapper(false);
    XmlMapper xmlMapper = new XmlMapper(xmlModule);
    AllGFlags data = xmlMapper.readValue(flagStream, AllGFlags.class);
    if (mostUsedGFlags) {
      InputStream inputStream =
          environment.resourceAsStream("gflags_metadata/" + "most_used_gflags.json");
      ObjectMapper mapper = new ObjectMapper();
      MostUsedGFlags freqUsedGFlags = mapper.readValue(inputStream, MostUsedGFlags.class);
      List<GFlagDetails> result = new ArrayList<>();
      for (GFlagDetails flag : data.flags) {
        if (serverType == ServerType.MASTER.name()) {
          if (freqUsedGFlags.masterGFlags.contains(flag.name)) {
            result.add(flag);
          }
        } else {
          if (freqUsedGFlags.tserverGFlags.contains(flag.name)) {
            result.add(flag);
          }
        }
      }
      return result;
    }
    return data.flags;
  }

  /** Structure to capture GFlags metadata from xml file. */
  private static class AllGFlags {
    @JsonProperty(value = "program")
    public String program;

    @JsonProperty(value = "usage")
    public String Usage;

    @JacksonXmlElementWrapper(useWrapping = false)
    @JsonProperty(value = "flag")
    public List<GFlagDetails> flags;
  }

  /** Structure to capture most used gflags from json file. */
  private static class MostUsedGFlags {
    @JsonProperty(value = "MASTER")
    List<String> masterGFlags;

    @JsonProperty(value = "TSERVER")
    List<String> tserverGFlags;
  }
}
