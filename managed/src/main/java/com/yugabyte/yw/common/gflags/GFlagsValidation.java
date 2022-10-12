// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.xml.JacksonXmlModule;
import com.fasterxml.jackson.dataformat.xml.XmlMapper;
import com.fasterxml.jackson.dataformat.xml.annotation.JacksonXmlElementWrapper;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.utils.FileUtils;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;

@Singleton
public class GFlagsValidation {

  private final Environment environment;

  private final RuntimeConfigFactory runtimeConfigFactory;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidation.class);

  public static final List<String> GFLAG_FILENAME_LIST =
      ImmutableList.of("master_flags.xml", "tserver_flags.xml");

  @Inject
  public GFlagsValidation(Environment environment, RuntimeConfigFactory runtimeConfigFactory) {
    this.environment = environment;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public List<GFlagDetails> extractGFlags(String version, String serverType, boolean mostUsedGFlags)
      throws IOException {
    String releasesPath =
        runtimeConfigFactory.staticApplicationConf().getString(Util.YB_RELEASES_PATH);
    File file =
        new File(
            String.format("%s/%s/%s_flags.xml", releasesPath, version, serverType.toLowerCase()));
    InputStream flagStream;
    if (Files.exists(Paths.get(file.getAbsolutePath()))) {
      flagStream = FileUtils.getInputStreamOrFail(file);
    } else {
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
        if (serverType.equals(ServerType.MASTER.name())) {
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

  public void fetchGFlagsFromDBPackage(
      String dbTarPackagePath, String dbVersion, List<String> requiredGFlagFileList) {
    String releasesPath =
        runtimeConfigFactory.staticApplicationConf().getString(Util.YB_RELEASES_PATH);
    try (TarArchiveInputStream tarInput =
        new TarArchiveInputStream(
            new GzipCompressorInputStream(new FileInputStream(dbTarPackagePath)))) {
      TarArchiveEntry currentEntry;
      while ((currentEntry = tarInput.getNextTarEntry()) != null) {
        // Ignore all non-flag xml files.
        if (!currentEntry.isFile() || !currentEntry.getName().endsWith("flags.xml")) {
          continue;
        }
        // Generally, we get the currentEntry variable value for the
        // gFlag file like `dbVersion/master_flags.xml`
        List<String> tarGFlagFilePathList = Arrays.asList(currentEntry.getName().split("/"));
        if (tarGFlagFilePathList.size() == 0) {
          continue;
        }
        String gFlagFileName = tarGFlagFilePathList.get(tarGFlagFilePathList.size() - 1);
        // Don't modify/re-write existing gFlags files, only add missing ones.
        if (!requiredGFlagFileList.contains(gFlagFileName)) {
          continue;
        }
        String absoluteGFlagFileName =
            String.format("%s/%s/%s", releasesPath, dbVersion, gFlagFileName);
        File gFlagOutputFile = new File(absoluteGFlagFileName);
        if (!gFlagOutputFile.exists()) {
          gFlagOutputFile.getParentFile().mkdirs();
          gFlagOutputFile.createNewFile();
          BufferedInputStream in = new BufferedInputStream(tarInput);
          ByteArrayOutputStream out = new ByteArrayOutputStream();
          IOUtils.copy(in, out);
          try (OutputStream outputStream = new FileOutputStream(gFlagOutputFile)) {
            out.writeTo(outputStream);
          } catch (Exception e) {
            LOG.error(
                "Caught an error while adding {} for DB version{}: {}",
                gFlagFileName,
                dbVersion,
                e);
          }
        }
      }
    } catch (Exception e) {
      LOG.error("Caught an error while adding gflags metadata for version: {}", dbVersion, e);
    }
  }

  private boolean checkGFlagFileExists(
      String releasesPath, String dbVersion, String gFlagFileName) {
    String filePath = String.format("%s/%s/%s", releasesPath, dbVersion, gFlagFileName);
    return Files.exists(Paths.get(filePath));
  }

  public List<String> getMissingGFlagFileList(String dbVersion) {
    String releasesPath =
        runtimeConfigFactory.staticApplicationConf().getString(Util.YB_RELEASES_PATH);
    return GFLAG_FILENAME_LIST
        .stream()
        .filter((gFlagFileName) -> !checkGFlagFileExists(releasesPath, dbVersion, gFlagFileName))
        .collect(Collectors.toList());
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
