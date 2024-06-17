// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.annotation.DbEnumValue;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Embeddable;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.IdClass;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import java.io.Serializable;
import java.util.UUID;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;

@Entity
@IdClass(XClusterNamespaceConfig.XClusterNamespaceConfigPK.class)
@Getter
@Setter
public class XClusterNamespaceConfig {

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

  public XClusterNamespaceConfig(XClusterConfig config, String sourceNamespaceId) {
    this.setConfig(config);
    this.setSourceNamespaceId(sourceNamespaceId);
    this.setStatus(Status.Validated);
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
