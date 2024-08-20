// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import io.ebean.Finder;
import io.ebean.annotation.DbEnumValue;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Embeddable;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.IdClass;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Transient;
import java.io.Serializable;
import java.util.Optional;
import java.util.UUID;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Entity
@IdClass(XClusterNamespaceConfig.XClusterNamespaceConfigPK.class)
@Getter
@Setter
public class XClusterNamespaceConfig {

  private static final Finder<String, XClusterNamespaceConfig> find =
      new Finder<String, XClusterNamespaceConfig>(XClusterNamespaceConfig.class) {};

  @Id
  @ManyToOne
  @JoinColumn(name = "config_uuid", referencedColumnName = "uuid")
  @JsonIgnore
  private XClusterConfig config;

  @Id
  @Column(length = 64)
  private String sourceNamespaceId;

  @ApiModelProperty(
      value = "Status",
      allowableValues = "Validated, Running, Updating, Warning, Error, Bootstrapping, Failed")
  private Status status;

  // Statuses are declared in reverse severity for showing tables in UI with specific order.
  public enum Status {
    Failed("Failed"),
    Error("Error"), // Not stored in YBA DB.
    Warning("Warning"), // Not stored in YBA DB.
    Updating("Updating"),
    Bootstrapping("Bootstrapping"),
    Validated("Validated"),
    Running("Running");

    private final String status;

    Status(String status) {
      this.status = status;
    }

    @Override
    @DbEnumValue
    public String toString() {
      return this.status;
    }
  }

  @Transient
  @ApiModelProperty(value = "namespaceInfo from source universe", required = false)
  private NamespaceInfoResp sourceNamespaceInfo;

  @Transient
  @ApiModelProperty(value = "namespaceInfo from target universe", required = false)
  private NamespaceInfoResp targetNamespaceInfo;

  public XClusterNamespaceConfig(XClusterConfig config, String sourceNamespaceId) {
    this.setConfig(config);
    this.setSourceNamespaceId(sourceNamespaceId);
    this.setStatus(Status.Validated);
  }

  /**
   * Retrieves an XClusterNamespaceConfig object based on the provided sourceNamespaceId.
   *
   * @param sourceNamespaceId The unique identifier of the namespace.
   * @return An Optional containing the XClusterNamespaceConfig object if found, or an empty
   *     Optional if not found.
   */
  public static Optional<XClusterNamespaceConfig> maybeGetByNamespaceId(String sourceNamespaceId) {
    XClusterNamespaceConfig xClusterNamespaceConfig =
        find.query().where().eq("source_namespace_id", sourceNamespaceId).findOne();
    if (xClusterNamespaceConfig == null) {
      log.info(
          "Cannot find an xClusterNamespaceConfig with sourceNamespaceId {}", sourceNamespaceId);
      return Optional.empty();
    }
    return Optional.of(xClusterNamespaceConfig);
  }

  /** This class is the primary key for XClusterNamespaceConfig. */
  @Embeddable
  @EqualsAndHashCode
  public static class XClusterNamespaceConfigPK implements Serializable {
    @Column(name = "config_uuid")
    public UUID config;

    public String sourceNamespaceId;
  }
}
