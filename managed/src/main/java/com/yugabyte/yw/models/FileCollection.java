// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbEnumValue;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

/**
 * Model for file collection metadata. Stores information about files collected from DB nodes via
 * the collect-files API. The collection_uuid is used to download and cleanup the files later.
 */
@Slf4j
@Entity
@Table(name = "file_collection")
public class FileCollection extends Model {

  private static final ObjectMapper MAPPER = new ObjectMapper();

  /** Status of a file collection */
  public enum Status {
    COLLECTED("COLLECTED"), // Tar files created on DB nodes, ready for download
    DOWNLOADING("DOWNLOADING"), // Currently downloading from nodes to YBA
    DOWNLOADED("DOWNLOADED"), // Files have been downloaded to YBA
    DB_NODES_CLEANED(
        "DB_NODES_CLEANED"), // Remote tar files deleted from DB nodes (YBA files may exist)
    YBA_CLEANED("YBA_CLEANED"), // YBA local files deleted (DB node files may exist)
    CLEANED_UP("CLEANED_UP"), // All files deleted from both DB nodes and YBA
    FAILED("FAILED"); // Something went wrong

    private final String status;

    Status(String status) {
      this.status = status;
    }

    @DbEnumValue
    public String getValue() {
      return this.status;
    }
  }

  @Id
  @Column(name = "collection_uuid", nullable = false, unique = true)
  @Getter
  private UUID collectionUuid;

  @Column(name = "customer_uuid", nullable = false)
  @Getter
  private UUID customerUuid;

  @Column(name = "universe_uuid", nullable = false)
  @Getter
  private UUID universeUuid;

  @Column(name = "node_tar_paths", nullable = false, columnDefinition = "TEXT")
  private String nodeTarPathsJson;

  @Column(name = "node_addresses", nullable = false, columnDefinition = "TEXT")
  private String nodeAddressesJson;

  @Column(name = "status", nullable = false)
  @Getter
  @Setter
  private Status status;

  @Column(name = "created_at", nullable = false)
  @Getter
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date createdAt;

  @Column(name = "downloaded_at")
  @Getter
  @Setter
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date downloadedAt;

  @Column(name = "output_path")
  @Getter
  @Setter
  private String outputPath;

  private static final Finder<UUID, FileCollection> find =
      new Finder<UUID, FileCollection>(FileCollection.class) {};

  /** Create a new FileCollection record */
  public static FileCollection create(
      UUID customerUuid,
      UUID universeUuid,
      Map<String, String> nodeTarPaths,
      Map<String, String> nodeAddresses) {
    FileCollection collection = new FileCollection();
    collection.collectionUuid = UUID.randomUUID();
    collection.customerUuid = customerUuid;
    collection.universeUuid = universeUuid;
    collection.setNodeTarPaths(nodeTarPaths);
    collection.setNodeAddresses(nodeAddresses);
    collection.status = Status.COLLECTED;
    collection.createdAt = new Date();
    collection.save();
    log.info(
        "Created file collection {} for universe {} with {} nodes",
        collection.collectionUuid,
        universeUuid,
        nodeTarPaths.size());
    return collection;
  }

  /** Get a FileCollection by UUID */
  public static Optional<FileCollection> maybeGet(UUID collectionUuid) {
    FileCollection collection = find.byId(collectionUuid);
    return Optional.ofNullable(collection);
  }

  /** Get a FileCollection by UUID or throw exception */
  public static FileCollection getOrBadRequest(UUID collectionUuid) {
    return maybeGet(collectionUuid)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Collection not found. Collection UUID: " + collectionUuid));
  }

  /** Get all collections for a universe */
  public static List<FileCollection> getByUniverse(UUID universeUuid) {
    return find.query().where().eq("universe_uuid", universeUuid).findList();
  }

  /** Get all active collections (not yet cleaned up) for a universe */
  public static List<FileCollection> getActiveByUniverse(UUID universeUuid) {
    return find.query()
        .where()
        .eq("universe_uuid", universeUuid)
        .ne("status", Status.CLEANED_UP)
        .findList();
  }

  /** Get node tar paths as a Map */
  @JsonIgnore
  public Map<String, String> getNodeTarPaths() {
    if (nodeTarPathsJson == null || nodeTarPathsJson.isEmpty()) {
      return new HashMap<>();
    }
    try {
      return MAPPER.readValue(nodeTarPathsJson, new TypeReference<Map<String, String>>() {});
    } catch (JsonProcessingException e) {
      log.error("Failed to parse nodeTarPaths JSON", e);
      return new HashMap<>();
    }
  }

  /** Set node tar paths from a Map */
  @JsonIgnore
  public void setNodeTarPaths(Map<String, String> nodeTarPaths) {
    try {
      this.nodeTarPathsJson = MAPPER.writeValueAsString(nodeTarPaths);
    } catch (JsonProcessingException e) {
      log.error("Failed to serialize nodeTarPaths", e);
      this.nodeTarPathsJson = "{}";
    }
  }

  /** Get node addresses as a Map */
  @JsonIgnore
  public Map<String, String> getNodeAddresses() {
    if (nodeAddressesJson == null || nodeAddressesJson.isEmpty()) {
      return new HashMap<>();
    }
    try {
      return MAPPER.readValue(nodeAddressesJson, new TypeReference<Map<String, String>>() {});
    } catch (JsonProcessingException e) {
      log.error("Failed to parse nodeAddresses JSON", e);
      return new HashMap<>();
    }
  }

  /** Set node addresses from a Map */
  @JsonIgnore
  public void setNodeAddresses(Map<String, String> nodeAddresses) {
    try {
      this.nodeAddressesJson = MAPPER.writeValueAsString(nodeAddresses);
    } catch (JsonProcessingException e) {
      log.error("Failed to serialize nodeAddresses", e);
      this.nodeAddressesJson = "{}";
    }
  }

  /** Mark this collection as being downloaded */
  public void markDownloading() {
    this.status = Status.DOWNLOADING;
    this.save();
  }

  /** Mark this collection as downloaded */
  public void markDownloaded(String outputPath) {
    this.status = Status.DOWNLOADED;
    this.downloadedAt = new Date();
    this.outputPath = outputPath;
    this.save();
  }

  /** Mark this collection as failed */
  public void markFailed() {
    this.status = Status.FAILED;
    this.save();
  }

  /** Mark this collection as DB nodes cleaned (remote files deleted from DB nodes) */
  public void markDbNodesCleaned() {
    // If YBA was already cleaned, mark as fully cleaned up
    if (this.status == Status.YBA_CLEANED) {
      this.status = Status.CLEANED_UP;
    } else {
      this.status = Status.DB_NODES_CLEANED;
    }
    this.save();
  }

  /** Mark this collection as YBA cleaned (local files deleted from YBA) */
  public void markYbaCleaned() {
    // If DB nodes were already cleaned, mark as fully cleaned up
    if (this.status == Status.DB_NODES_CLEANED) {
      this.status = Status.CLEANED_UP;
    } else {
      this.status = Status.YBA_CLEANED;
    }
    this.save();
  }

  /** Mark this collection as cleaned up (all files deleted from both locations) */
  public void markCleanedUp() {
    this.status = Status.CLEANED_UP;
    this.save();
  }

  /** Check if this collection has been cleaned up */
  @JsonIgnore
  public boolean isCleanedUp() {
    return this.status == Status.CLEANED_UP;
  }

  /** Check if DB nodes have been cleaned */
  @JsonIgnore
  public boolean isDbNodesCleaned() {
    return this.status == Status.DB_NODES_CLEANED || this.status == Status.CLEANED_UP;
  }

  /** Check if YBA local files have been cleaned */
  @JsonIgnore
  public boolean isYbaCleaned() {
    return this.status == Status.YBA_CLEANED || this.status == Status.CLEANED_UP;
  }
}
