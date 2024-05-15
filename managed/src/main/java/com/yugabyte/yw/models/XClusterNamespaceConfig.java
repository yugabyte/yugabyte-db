// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
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

  public XClusterNamespaceConfig(XClusterConfig config, String sourceNamespaceId) {
    this.setConfig(config);
    this.setSourceNamespaceId(sourceNamespaceId);

    // TODO: Add statuses, etc
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
