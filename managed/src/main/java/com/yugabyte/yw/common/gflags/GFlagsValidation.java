// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.xml.JacksonXmlModule;
import com.fasterxml.jackson.dataformat.xml.XmlMapper;
import com.fasterxml.jackson.dataformat.xml.annotation.JacksonXmlElementWrapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
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
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.EqualsAndHashCode;
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

  public static final String YSQL_MIGRATION_FILES_LIST_FILE_NAME = "ysql_migration_files_list.json";

  public static final String MASTER_GFLAG_FILE_NAME = "master_flags.xml";

  public static final String TSERVER_GFLAG_FILE_NAME = "tserver_flags.xml";

  private static final String GLIBC_VERSION_FIELD_NAME = "glibc_v";

  // Skip these test auto flags while computing auto flags in YBA.
  public static final Set<String> TEST_AUTO_FLAGS =
      ImmutableSet.of("TEST_auto_flags_new_install", "TEST_auto_flags_initialized");

  public static final List<String> GFLAG_FILENAME_LIST =
      ImmutableList.of(
          MASTER_GFLAG_FILE_NAME,
          TSERVER_GFLAG_FILE_NAME,
          Util.AUTO_FLAG_FILENAME,
          Util.DB_VERSION_METADATA_FILENAME,
          YSQL_MIGRATION_FILES_LIST_FILE_NAME);

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
        try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
          String gFlagFileName =
              serverType.equals(ServerType.MASTER.name())
                  ? MASTER_GFLAG_FILE_NAME
                  : TSERVER_GFLAG_FILE_NAME;
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
            environment.resourceAsStream("gflags_metadata/most_used_gflags.json");
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

  public List<GFlagGroup> extractGFlagGroups(String version) throws IOException {
    InputStream flagStream = null;
    try {
      String majorVersion = version.substring(0, StringUtils.ordinalIndexOf(version, ".", 2));
      flagStream =
          environment.resourceAsStream(
              "gflag_groups/" + majorVersion + "/" + Util.GFLAG_GROUPS_FILENAME);
      if (flagStream == null) {
        LOG.error("GFlag groups metadata file for " + majorVersion + " is not present");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "GFlag groups metadata file for " + majorVersion + " is not present");
      }
      ObjectMapper mapper = new ObjectMapper();
      List<GFlagGroup> data =
          mapper.readValue(flagStream, new TypeReference<List<GFlagGroup>>() {});
      return data;
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
    LOG.info("Adding {} files for DB version {}", requiredGFlagFileList, dbVersion);
    YsqlMigrationFilesList migrationFilesList = new YsqlMigrationFilesList();
    try (TarArchiveInputStream tarInput =
        new TarArchiveInputStream(new GzipCompressorInputStream(inputStream))) {
      TarArchiveEntry currentEntry;
      while ((currentEntry = tarInput.getNextTarEntry()) != null) {
        if (isYSQLMigrationFile(currentEntry.getName())) {
          String migrationFileName = getYsqlMigrationFiles(currentEntry.getName());
          migrationFilesList.ysqlMigrationsFilesList.add(migrationFileName);
          continue;
        }

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
      if (requiredGFlagFileList.contains(YSQL_MIGRATION_FILES_LIST_FILE_NAME)) {
        File ysqlMigrationFileListFile =
            new File(
                String.format(
                    "%s/%s/%s", releasesPath, dbVersion, YSQL_MIGRATION_FILES_LIST_FILE_NAME));
        if (!Files.exists(Paths.get(ysqlMigrationFileListFile.getAbsolutePath()))) {
          ysqlMigrationFileListFile.getParentFile().mkdirs();
          ysqlMigrationFileListFile.createNewFile();
          ObjectMapper mapper = new ObjectMapper();
          FileUtils.writeJsonFile(
              ysqlMigrationFileListFile.getAbsolutePath(),
              (JsonNode) mapper.valueToTree(migrationFilesList));
        }
      }
    } catch (IOException e) {
      LOG.error("Caught an error while adding DB metadata for version: {}", dbVersion, e);
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
    List<String> missingFiles = getMissingFlagFiles(version);
    if (missingFiles.size() == 0) {
      return;
    }
    LOG.info("Adding {} files for version: {}", missingFiles, version);
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    try (InputStream tarGZIPInputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
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
    if (missingFiles.contains(YSQL_MIGRATION_FILES_LIST_FILE_NAME)
        && !CommonUtils.isReleaseEqualOrAfter(Util.YBDB_ROLLBACK_DB_VERSION, version)) {
      missingFiles.remove(YSQL_MIGRATION_FILES_LIST_FILE_NAME);
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
      try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
        fetchGFlagFilesFromTarGZipInputStream(
            inputStream, version, Collections.singletonList(Util.AUTO_FLAG_FILENAME), releasesPath);
      } catch (Exception e) {
        LOG.error("Error in extracting flags from DB package: ", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error in extracting flags from DB package");
      }
    }
    ObjectMapper objectMapper = new ObjectMapper();
    try (InputStream inputStream = FileUtils.getInputStreamOrFail(autoFlagFile)) {
      AutoFlags data = objectMapper.readValue(inputStream, AutoFlags.class);
      AutoFlagsPerServer autoFlags =
          data.autoFlagsPerServers.stream()
              .filter(flags -> flags.serverType.equals(serverType))
              .findFirst()
              .get();
      if (autoFlags != null) {
        autoFlags.autoFlagDetails =
            autoFlags.autoFlagDetails.stream()
                .filter(flag -> !TEST_AUTO_FLAGS.contains(flag.name))
                .collect(Collectors.toList());
      }
      return autoFlags;
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
    return fileName.endsWith("flags.xml")
        || fileName.endsWith(Util.AUTO_FLAG_FILENAME)
        || fileName.endsWith(Util.DB_VERSION_METADATA_FILENAME);
  }

  private boolean isYSQLMigrationFile(String fileName) {
    return fileName.contains("ysql_migration") && fileName.endsWith(".sql");
  }

  private String getYsqlMigrationFiles(String fileFullPath) {
    List<String> migrationFileNameList = Arrays.asList(fileFullPath.split("/"));
    String migrationFileName = migrationFileNameList.get(migrationFileNameList.size() - 1);
    return migrationFileName.substring(migrationFileName.indexOf("__") + 2);
  }

  public Set<String> getYsqlMigrationFilesList(String version) throws IOException {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    String filePath =
        String.format("%s/%s/%s", releasesPath, version, YSQL_MIGRATION_FILES_LIST_FILE_NAME);
    File file = new File(filePath);
    if (!Files.exists(Paths.get(file.getAbsolutePath()))) {
      try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
        fetchGFlagFilesFromTarGZipInputStream(
            inputStream,
            version,
            Collections.singletonList(YSQL_MIGRATION_FILES_LIST_FILE_NAME),
            releasesPath);
      } catch (Exception e) {
        LOG.error("Error in extracting YSQL migration from DB package", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error in extracting YSQL migration form DB package");
      }
    }
    ObjectMapper objectMapper = new ObjectMapper();
    try (InputStream inputStream = FileUtils.getInputStreamOrFail(file)) {
      YsqlMigrationFilesList data =
          objectMapper.readValue(inputStream, YsqlMigrationFilesList.class);
      return data.ysqlMigrationsFilesList;
    }
  }

  public Optional<Double> getGlibcVersion(String version) throws IOException {
    String releasesPath = confGetter.getStaticConf().getString(Util.YB_RELEASES_PATH);
    String filePath =
        String.format("%s/%s/%s", releasesPath, version, Util.DB_VERSION_METADATA_FILENAME);
    File file = new File(filePath);
    if (!Files.exists(Paths.get(file.getAbsolutePath()))) {
      try (InputStream inputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
        fetchGFlagFilesFromTarGZipInputStream(
            inputStream,
            version,
            Collections.singletonList(Util.DB_VERSION_METADATA_FILENAME),
            releasesPath);
      } catch (Exception e) {
        LOG.error("Error in extracting version metadata from DB package", e);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error in extracting version metadata form DB package");
      }
    }
    ObjectMapper objectMapper = new ObjectMapper();
    JsonNode jsonNode = objectMapper.readTree(file);
    if (jsonNode.has(GLIBC_VERSION_FIELD_NAME)) {
      String glibc = jsonNode.get(GLIBC_VERSION_FIELD_NAME).asText();
      return Optional.of(Double.parseDouble(glibc));
    } else {
      return Optional.empty();
    }
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

  @EqualsAndHashCode(callSuper = false)
  public static class AutoFlagDetails {
    @JsonAlias(value = "name")
    public String name;

    @JsonAlias(value = "class")
    public int flagClass;

    @JsonAlias(value = "is_runtime")
    public boolean runtime;
  }

  public static class YsqlMigrationFilesList {
    @JsonAlias(value = "ysqlMigrationsFilesList")
    public Set<String> ysqlMigrationsFilesList = new HashSet<>();
  }
}
