// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.xml.JacksonXmlModule;
import com.fasterxml.jackson.dataformat.xml.XmlMapper;
import com.fasterxml.jackson.dataformat.xml.annotation.JacksonXmlElementWrapper;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import org.apache.commons.collections4.MapUtils;
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

  private final RuntimeConfGetter confGetter;

  private final ReleaseManager releaseManager;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidation.class);

  public static final List<String> GFLAG_FILENAME_LIST =
      ImmutableList.of("master_flags.xml", "tserver_flags.xml", "auto_flags.json");

  public static final String DB_BUILD_WITH_FLAG_FILES = "2.17.0.0-b1";

  @Inject
  public GFlagsValidation(
      Environment environment, RuntimeConfGetter confGetter, ReleaseManager releaseManager) {
    this.environment = environment;
    this.confGetter = confGetter;
    this.releaseManager = releaseManager;
  }

  public List<GFlagDetails> extractGFlags(String version, String serverType, boolean mostUsedGFlags)
      throws IOException {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    File file =
        new File(
            String.format("%s/%s/%s_flags.xml", releasesPath, version, serverType.toLowerCase()));
    InputStream flagStream = null;
    try {
      if (Files.exists(Paths.get(file.getAbsolutePath()))) {
        flagStream = FileUtils.getInputStreamOrFail(file);
      } else if (CommonUtils.isReleaseEqualOrAfter(DB_BUILD_WITH_FLAG_FILES, version)) {
        // Fetch gFlags files from DB package if it does not exist
        ReleaseManager.ReleaseMetadata rm = releaseManager.getReleaseByVersion(version);
        try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version, rm)) {
          String gFlagFileName = serverType.toLowerCase() + "_flags.xml";
          fetchGFlagFilesFromTarGZipInputStream(
              inputStream, version, Collections.singletonList(gFlagFileName), releasesPath);
          flagStream = FileUtils.getInputStreamOrFail(file);
        } catch (Exception e) {
          LOG.error("Error in extracting flags from DB package", e);
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Error in extracting flags form DB package");
        }
      } else {
        // Use static flag files for old versions.
        String majorVersion = version.substring(0, StringUtils.ordinalIndexOf(version, ".", 2));
        flagStream =
            environment.resourceAsStream(
                "gflags_metadata/" + majorVersion + "/" + serverType.toLowerCase() + ".xml");
        if (flagStream == null) {
          LOG.error("GFlags metadata file for " + majorVersion + " is not present");
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              "GFlags metadata file for " + majorVersion + " is not present");
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
    } finally {
      if (flagStream != null) {
        flagStream.close();
      }
    }
  }

  public void fetchGFlagFilesFromTarGZipInputStream(
      InputStream inputStream,
      String dbVersion,
      List<String> requiredGFlagFileList,
      String releasesPath)
      throws IOException {
    try (TarArchiveInputStream tarInput =
        new TarArchiveInputStream(new GzipCompressorInputStream(inputStream))) {
      TarArchiveEntry currentEntry;
      while ((currentEntry = tarInput.getNextTarEntry()) != null) {
        // Ignore all non-flag xml and auto flags files.
        if (!currentEntry.isFile() || !isFlagFile(currentEntry.getName())) {
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
          } catch (IOException e) {
            LOG.error(
                "Caught an error while adding {} for DB version{}: {}",
                gFlagFileName,
                dbVersion,
                e);
            throw e;
          }
        }
      }
    } catch (IOException e) {
      LOG.error("Caught an error while adding gFlags metadata for version: {}", dbVersion, e);
      throw e;
    }
  }

  public boolean checkGFlagFileExists(String releasesPath, String dbVersion, String gFlagFileName) {
    String filePath = String.format("%s/%s/%s", releasesPath, dbVersion, gFlagFileName);
    return Files.exists(Paths.get(filePath));
  }

  public AutoFlagsPerServer extractAutoFlags(String version, ServerType serverType)
      throws IOException {
    if (serverType.equals(ServerType.MASTER)) {
      return extractAutoFlags(version, "yb-master");
    } else if (serverType.equals(ServerType.TSERVER)) {
      return extractAutoFlags(version, "yb-tserver");
    }
    return null;
  }

  public void addDBMetadataFiles(String version) {
    ReleaseManager.ReleaseMetadata rm = releaseManager.getReleaseByVersion(version);
    addDBMetadataFiles(version, rm);
  }

  public void addDBMetadataFiles(String version, ReleaseManager.ReleaseMetadata rm) {
    List<String> missingFiles = getMissingFlagFiles(version);
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    try (InputStream tarGZIPInputStream =
        releaseManager.getTarGZipDBPackageInputStream(version, rm)) {
      fetchGFlagFilesFromTarGZipInputStream(
          tarGZIPInputStream, version, missingFiles, releasesPath);
    } catch (Exception e) {
      LOG.error("Error in fetching GFlags metadata: ", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error in adding DB metadata files for DB version " + version);
    }
  }

  private List<String> getMissingFlagFiles(String version) {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    List<String> missingFiles = new ArrayList<>();
    for (String fileName : GFlagsValidation.GFLAG_FILENAME_LIST) {
      File file = new File(String.format("%s/%s/%s", releasesPath, version, fileName));
      if (!Files.exists(Paths.get(file.getAbsolutePath()))) {
        missingFiles.add(fileName);
      }
    }
    if (missingFiles.contains(Util.AUTO_FLAG_FILENAME)
        && !CommonUtils.isAutoFlagSupported(version)) {
      missingFiles.remove(Util.AUTO_FLAG_FILENAME);
    }
    return missingFiles;
  }

  /**
   * Returns list of auto flags from auto_flags.json. This list contains all auto flags present in a
   * version.
   *
   * @param version
   * @param serverType
   * @return
   * @throws IOException
   */
  public AutoFlagsPerServer extractAutoFlags(String version, String serverType) throws IOException {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    File autoFlagFile = Paths.get(releasesPath, version, Util.AUTO_FLAG_FILENAME).toFile();
    if (!Files.exists(Paths.get(autoFlagFile.getAbsolutePath()))) {
      ReleaseManager.ReleaseMetadata rm = releaseManager.getReleaseByVersion(version);
      try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version, rm)) {
        fetchGFlagFilesFromTarGZipInputStream(
            inputStream, version, Collections.singletonList(Util.AUTO_FLAG_FILENAME), releasesPath);
      } catch (Exception e) {
        LOG.error("Error in extracting flags form DB package: ", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error in extracting flags form DB package");
      }
    }
    ObjectMapper objectMapper = new ObjectMapper();
    try (InputStream inputStream = FileUtils.getInputStreamOrFail(autoFlagFile)) {
      AutoFlags data = objectMapper.readValue(inputStream, AutoFlags.class);
      return data.autoFlagsPerServers.stream()
          .filter(flags -> flags.serverType.equals(serverType))
          .findFirst()
          .get();
    }
  }

  public Map<String, String> getFilteredAutoFlagsWithNonInitialValue(
      Map<String, String> flags, String version, ServerType serverType) throws IOException {
    Map<String, String> filteredList = new HashMap<>();
    if (MapUtils.isEmpty(flags)) {
      return filteredList;
    }
    Map<String, GFlagDetails> allGFlagsMap =
        extractGFlags(version, serverType.name(), false).stream()
            .collect(Collectors.toMap(flagDetails -> flagDetails.name, Function.identity()));
    for (Map.Entry<String, String> entry : flags.entrySet()) {
      String flag = entry.getKey();
      if (!allGFlagsMap.containsKey(flag)) {
        throw new PlatformServiceException(BAD_REQUEST, flag + " is not present in metadata.");
      }
      GFlagDetails flagDetail = allGFlagsMap.get(flag);
      if (isAutoFlag(flagDetail) && !flagDetail.initial.equals(entry.getValue())) {
        filteredList.put(entry.getKey(), entry.getValue());
      }
    }
    return filteredList;
  }

  /**
   * Return list of auto flags from gflags metadata files. The list might not contains hidden auto
   * flags.
   *
   * @param version
   * @param serverType
   * @return
   * @throws IOException
   */
  public List<GFlagDetails> listAllAutoFlags(String version, String serverType) throws IOException {
    List<GFlagDetails> allGFlags = extractGFlags(version, serverType, false /* mostUsedGFlags */);
    return allGFlags.stream().filter(flag -> isAutoFlag(flag)).collect(Collectors.toList());
  }

  private Set<String> getFlagsTagSet(GFlagDetails flagDetails) {
    if (StringUtils.isEmpty(flagDetails.tags)) {
      return new HashSet<>();
    }
    return new HashSet<>(Arrays.asList(StringUtils.splitPreserveAllTokens(flagDetails.tags, ",")));
  }

  public boolean isAutoFlag(GFlagDetails flag) {
    return getFlagsTagSet(flag).contains("auto");
  }

  private boolean isFlagFile(String fileName) {
    return fileName.endsWith("flags.xml") || fileName.endsWith(Util.AUTO_FLAG_FILENAME);
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

  /** Structure to capture Auto Flags details from json file */
  public static class AutoFlags {
    @JsonProperty(value = "auto_flags")
    public List<AutoFlagsPerServer> autoFlagsPerServers;
  }

  public static class AutoFlagsPerServer {
    @JsonAlias(value = "program")
    public String serverType;

    @JsonAlias(value = "flags")
    public List<AutoFlagDetails> autoFlagDetails;
  }

  public static class AutoFlagDetails {
    @JsonAlias(value = "name")
    public String name;

    @JsonAlias(value = "class")
    public int flagClass;

    @JsonAlias(value = "is_runtime")
    public boolean runtime;
  }
}
