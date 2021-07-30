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

import org.apache.commons.collections.CollectionUtils;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;

import io.ebean.Finder;
import io.ebean.Model;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import play.data.validation.Constraints;

@EqualsAndHashCode(callSuper = false, doNotUseGetters = true)
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

  @Constraints.Required
  @Column(nullable = false)
  private boolean defaultRoute;

  private static final Finder<UUID, AlertRoute> find =
      new Finder<UUID, AlertRoute>(AlertRoute.class) {};

  public static AlertRoute create(UUID customerUUID, String name, List<AlertReceiver> receivers) {
    return create(customerUUID, name, receivers, false);
  }

  public static AlertRoute create(
      UUID customerUUID, String name, List<AlertReceiver> receivers, boolean isDefault) {
    if (CollectionUtils.isEmpty(receivers)) {
      throw new IllegalArgumentException("Can't create alert route without receivers");
    }

    AlertRoute route = new AlertRoute();
    route.customerUUID = customerUUID;
    route.uuid = UUID.randomUUID();
    route.name = name;
    route.receivers = new HashSet<>(receivers);
    route.defaultRoute = isDefault;
    route.save();
    return route;
  }

  public UUID getUuid() {
    return uuid;
  }

  public void setUuid(UUID uuid) {
    this.uuid = uuid;
  }

  public UUID getCustomerUUID() {
    return customerUUID;
  }

  public void setCustomerUUID(UUID customerUUID) {
    this.customerUUID = customerUUID;
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

  public boolean isDefaultRoute() {
    return defaultRoute;
  }

  public void setDefaultRoute(boolean isDefault) {
    defaultRoute = isDefault;
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

  public static AlertRoute getDefaultRoute(UUID customerUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("default_route", true)
        .findOne();
  }

  /**
   * Creates default route for the specified customer. Created route has only one receiver of type
   * Email with the set of passed recipients. Also it doesn't have own SMTP configuration and uses
   * default SMTP settings (from the platform configuration).
   *
   * @param customerUUID
   * @param recipients
   * @return
   */
  public static AlertRoute createDefaultRoute(UUID customerUUID) {
    AlertReceiverEmailParams defaultParams = new AlertReceiverEmailParams();
    defaultParams.defaultSmtpSettings = true;
    defaultParams.defaultRecipients = true;
    AlertReceiver defaultReceiver =
        AlertReceiver.create(customerUUID, "Default Receiver", defaultParams);
    AlertRoute route =
        AlertRoute.create(
            customerUUID, "Default Route", Collections.singletonList(defaultReceiver));
    route.setDefaultRoute(true);
    route.save();
    return route;
  }
}
