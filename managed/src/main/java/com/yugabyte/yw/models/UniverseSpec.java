// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.utils.FileUtils;
import io.ebean.annotation.Transactional;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.zip.GZIPOutputStream;
import lombok.Builder;
import lombok.Data;
import lombok.extern.jackson.Jacksonized;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.io.FilenameUtils;
import play.libs.Json;

@Slf4j
@Builder
@Jacksonized
@Data
public class UniverseSpec {

  private static final String PEM_PERMISSIONS = "r--------";
  private static final String PUB_PERMISSIONS = "r--------";
  private static final String DEFAULT_PERMISSIONS = "rw-r--r--";

  public Universe universe;

  // Already consists of regions and regions contain availability zones
  public Provider provider;

  public List<AccessKey> accessKeys;

  public List<InstanceType> instanceTypes;

  public List<PriceComponent> priceComponents;

  public Map<String, String> universeConfig;

  private String oldStoragePath;

  public InputStream exportSpec() throws IOException {
    String specBasePath = this.oldStoragePath + "/universe-specs/export";
    String specName = UniverseSpec.generateSpecName(true);
    String specJsonPath = specBasePath + "/" + specName + ".json";
    String specTarGZPath = specBasePath + "/" + specName + ".tar.gz";
    Files.createDirectories(Paths.get(specBasePath));

    File jsonSpecFile = new File(specJsonPath);
    exportUniverseSpecObj(jsonSpecFile);

    try (FileOutputStream fos = new FileOutputStream(specTarGZPath);
        GZIPOutputStream gos = new GZIPOutputStream(new BufferedOutputStream(fos));
        TarArchiveOutputStream tarOS = new TarArchiveOutputStream(gos)) {
      tarOS.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX);

      // Save universe spec.
      Util.copyFileToTarGZ(jsonSpecFile, "universe-spec.json", tarOS);

      // Save access keys.
      for (AccessKey accessKey : accessKeys) {
        exportAccessKey(accessKey, tarOS);
      }
    }

    Files.deleteIfExists(jsonSpecFile.toPath());
    InputStream is =
        FileUtils.getInputStreamOrFail(new File(specTarGZPath), true /* deleteOnClose */);
    return is;
  }

  private void exportUniverseSpecObj(File jsonSpecFile) throws IOException {
    ObjectMapper mapper = Json.mapper();
    ObjectNode universeSpecObj = (ObjectNode) Json.toJson(this);
    universeSpecObj = setIgnoredJsonProperties(universeSpecObj);
    mapper.writeValue(jsonSpecFile, universeSpecObj);
  }

  private void exportAccessKey(AccessKey accessKey, TarArchiveOutputStream tarArchive)
      throws IOException {
    String pubKeyPath = accessKey.getKeyInfo().publicKey;
    File pubKeyFile = new File(pubKeyPath);
    File accessKeyFolder = pubKeyFile.getParentFile();
    Util.addFilesToTarGZ(accessKeyFolder.getAbsolutePath(), "keys/", tarArchive);
  }

  // Add any member variables that have the @jsonIgnored annotation.
  public ObjectNode setIgnoredJsonProperties(ObjectNode universeSpecObj) {
    JsonNode providerUnmaskedConfig = Json.toJson(this.provider.getUnmaskedConfig());
    ObjectNode providerObj = (ObjectNode) universeSpecObj.get("provider");
    providerObj.set("config", providerUnmaskedConfig);

    List<Region> regions = this.provider.regions;
    ArrayNode regionsObj = (ArrayNode) providerObj.get("regions");

    if (regionsObj.isArray()) {
      for (int i = 0; i < regions.size(); i++) {
        ObjectNode regionObj = (ObjectNode) regionsObj.get(i);
        Region region = regions.get(i);
        JsonNode regionUnmaskedConfig = Json.toJson(region.getUnmaskedConfig());
        regionObj.set("config", regionUnmaskedConfig);

        List<AvailabilityZone> zones = region.zones;
        ArrayNode zonesObj = (ArrayNode) regionObj.get("zones");

        if (zonesObj.isArray()) {
          for (int j = 0; j < zones.size(); j++) {
            ObjectNode zoneObj = (ObjectNode) zonesObj.get(j);
            AvailabilityZone zone = zones.get(j);
            JsonNode zoneUnmaskedConfig = Json.toJson(zone.getUnmaskedConfig());
            zoneObj.set("config", zoneUnmaskedConfig);
          }
        }
      }
    }
    return universeSpecObj;
  }

  public static UniverseSpec importSpec(File tarFile, String storagePath) throws IOException {
    String specBasePath = storagePath + "/universe-specs/import";
    String specName = UniverseSpec.generateSpecName(false);
    String specFolderPath = specBasePath + "/" + specName;
    Files.createDirectories(Paths.get(specFolderPath));

    Util.extractFilesFromTarGZ(tarFile, specFolderPath);

    // Retrieve universe spec.
    UniverseSpec universeSpec = UniverseSpec.importUniverseSpec(specFolderPath);

    // Copy access keys to correct location if existing provider does not exist.
    universeSpec.importAccessKeys(specFolderPath, storagePath);

    return universeSpec;
  }

  private static UniverseSpec importUniverseSpec(String specFolderPath) throws IOException {
    File jsonDir = new File(specFolderPath + "/universe-spec.json");
    ObjectMapper mapper = Json.mapper();
    UniverseSpec universeSpec = mapper.readValue(jsonDir, UniverseSpec.class);
    return universeSpec;
  }

  private void importAccessKeys(String specFolderPath, String storagePath) throws IOException {
    if (!Provider.maybeGet(provider.uuid).isPresent()) {
      File srcKeyMasterDir = new File(specFolderPath + "/keys");
      File[] keyDirs = srcKeyMasterDir.listFiles();
      if (keyDirs != null) {
        for (File keyDir : keyDirs) {
          File[] keyFiles = keyDir.listFiles();
          if (keyFiles != null) {
            for (File keyFile : keyFiles) {
              String extension = FilenameUtils.getExtension(keyFile.getName());
              Set<PosixFilePermission> permissions =
                  PosixFilePermissions.fromString(DEFAULT_PERMISSIONS);
              if (extension.equals("pem")) {
                permissions = PosixFilePermissions.fromString(PEM_PERMISSIONS);
              } else if (extension.equals("pub")) {
                permissions = PosixFilePermissions.fromString(PUB_PERMISSIONS);
              }
              Files.setPosixFilePermissions(keyFile.toPath(), permissions);
            }
          }
        }
      }
      File destKeyMasterDir = new File(storagePath + "/keys");
      org.apache.commons.io.FileUtils.copyDirectory(srcKeyMasterDir, destKeyMasterDir);
      log.debug("Saved access keys to {}", destKeyMasterDir.getPath());
    }
  }

  @Transactional
  public void save(String storagePath) {
    // Check if provider exists, if not, save all entities related to provider.
    if (!Provider.maybeGet(this.provider.uuid).isPresent()) {
      this.provider.save();

      for (InstanceType instanceType : this.instanceTypes) {
        instanceType.save();
      }

      for (PriceComponent priceComponent : this.priceComponents) {
        priceComponent.save();
      }

      for (AccessKey key : this.getAccessKeys()) {
        key.getKeyInfo().publicKey =
            key.getKeyInfo().publicKey.replace(this.oldStoragePath, storagePath);
        key.getKeyInfo().privateKey =
            key.getKeyInfo().privateKey.replace(this.oldStoragePath, storagePath);
        key.getKeyInfo().vaultPasswordFile =
            key.getKeyInfo().vaultPasswordFile.replace(this.oldStoragePath, storagePath);
        key.getKeyInfo().vaultFile =
            key.getKeyInfo().vaultFile.replace(this.oldStoragePath, storagePath);
      }

      for (AccessKey key : this.accessKeys) {
        key.save();
      }
    }

    this.universe.setConfig(this.universeConfig);
    this.universe.save();
  }

  private static String generateSpecName(boolean isExport) {
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String type = isExport ? "export" : "import";
    String specName = "yb-universe-spec-" + type + "-" + datePrefix;
    return specName;
  }
}
