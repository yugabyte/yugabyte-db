// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.FetchType;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.JoinTable;
import javax.persistence.ManyToMany;
import lombok.AccessLevel;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;
import lombok.ToString;
import lombok.experimental.Accessors;
import play.data.validation.Constraints;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false, doNotUseGetters = true)
@Entity
public class AlertRoute extends Model {

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  @Constraints.Required
  @Column(columnDefinition = "Text", length = 255, nullable = false)
  private String name;

  @ToString.Exclude
  @Getter(AccessLevel.NONE)
  @Setter(AccessLevel.NONE)
  @ManyToMany(fetch = FetchType.LAZY)
  @JoinTable(
      name = "alert_route_group",
      joinColumns = {
        @JoinColumn(
            name = "route_uuid",
            referencedColumnName = "uuid",
            nullable = false,
            updatable = false)
      },
      inverseJoinColumns = {
        @JoinColumn(
            name = "receiver_uuid",
            referencedColumnName = "uuid",
            nullable = false,
            updatable = false)
      })
  private Set<AlertReceiver> receivers;

  @Constraints.Required
  @Column(nullable = false)
  private boolean defaultRoute = false;

  private static final Finder<UUID, AlertRoute> find =
      new Finder<UUID, AlertRoute>(AlertRoute.class) {};

  @JsonProperty
  public List<UUID> getReceivers() {
    return receivers.stream().map(AlertReceiver::getUuid).collect(Collectors.toList());
  }

  @JsonIgnore
  public List<AlertReceiver> getReceiversList() {
    return new ArrayList<>(receivers);
  }

  public AlertRoute setReceiversList(@NonNull List<AlertReceiver> receivers) {
    this.receivers = new HashSet<>(receivers);
    return this;
  }

  public AlertRoute generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  public static AlertRoute get(UUID customerUUID, UUID routeUUID) {
    return createQuery().idEq(routeUUID).eq("customer_uuid", customerUUID).findOne();
  }

  public static ExpressionList<AlertRoute> createQuery() {
    return find.query().where();
  }
}
