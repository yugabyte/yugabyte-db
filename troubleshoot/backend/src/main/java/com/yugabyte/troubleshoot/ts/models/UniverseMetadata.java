// Copyright (c) Yugabyte, Inc.
package com.yugabyte.troubleshoot.ts.models;

import io.ebean.Model;
import jakarta.validation.constraints.NotNull;
import java.util.UUID;
import javax.persistence.Entity;
import javax.persistence.Id;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import org.hibernate.validator.constraints.URL;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
public class UniverseMetadata extends Model implements ModelWithId<UUID> {

  @Id @NotNull private UUID id;

  @NotNull private UUID customerId;

  @NotNull private String apiToken;

  // TODO will need to think of HA scenarios.
  @NotNull @URL private String platformUrl;

  @NotNull @URL private String metricsUrl;
}
