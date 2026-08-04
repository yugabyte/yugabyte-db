// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.Model;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.ManyToOne;
import java.util.UUID;
import lombok.Data;

@Entity
@Data
public class Webhook extends Model {

  @Id private UUID uuid;

  private String url;

  @ManyToOne @JsonIgnore private DrConfig drConfig;

  public Webhook(DrConfig drConfig, String url) {
    this.drConfig = drConfig;
    this.url = url;
    this.uuid = UUID.randomUUID();
  }
}
