// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import java.util.ArrayList;
import java.util.Collections;
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

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.YWServiceException;

import io.ebean.Finder;
import io.ebean.Model;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import play.data.validation.Constraints;

@EqualsAndHashCode(callSuper = false)
@ToString
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

  private static final Finder<UUID, AlertRoute> find =
      new Finder<UUID, AlertRoute>(AlertRoute.class) {};

  /**
   * Creates AlertRoute between the alert definition and the alert receiver. Fails if the receiver
   * or the definition are not found, or if the definition customer UUID doesn't match the passed
   * customer UUID.
   *
   * @param customerUUID
   * @param name
   * @param receivers
   * @return
   */
  public static AlertRoute create(UUID customerUUID, String name, List<AlertReceiver> receivers) {
    AlertRoute route = new AlertRoute();
    route.customerUUID = customerUUID;
    route.uuid = UUID.randomUUID();
    route.name = name;
    route.receivers = new HashSet<>(receivers);
    route.save();
    return route;
  }

  public UUID getUuid() {
    return uuid;
  }

  public void setUUID(UUID uuid) {
    this.uuid = uuid;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  @JsonProperty
  public List<UUID> getReceivers() {
    return receivers.stream().map(AlertReceiver::getUuid).collect(Collectors.toList());
  }

  @JsonIgnore
  public List<AlertReceiver> getReceiversList() {
    return new ArrayList<>(receivers);
  }

  public void setReceiversList(List<AlertReceiver> receivers) {
    this.receivers = new HashSet<>(receivers);
  }

  public static AlertRoute get(UUID customerUUID, UUID routeUUID) {
    return find.query().where().idEq(routeUUID).eq("customer_uuid", customerUUID).findOne();
  }

  public static AlertRoute getOrBadRequest(UUID customerUUID, UUID routeUUID) {
    AlertRoute alertRoute = get(customerUUID, routeUUID);
    if (alertRoute == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Route UUID: " + routeUUID);
    }
    return alertRoute;
  }

  public static List<AlertRoute> listByCustomer(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }
}
