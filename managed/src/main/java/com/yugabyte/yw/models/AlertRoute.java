// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;

import com.yugabyte.yw.common.YWServiceException;

import io.ebean.Finder;
import io.ebean.Model;
import play.data.validation.Constraints;

@Entity
public class AlertRoute extends Model {

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @ManyToOne(optional = false)
  @JoinColumn(name = "definition_uuid")
  private UUID definitionUUID;

  @ManyToOne(optional = false)
  @JoinColumn(name = "receiver_uuid")
  private UUID receiverUUID;

  private static final Finder<UUID, AlertRoute> find =
      new Finder<UUID, AlertRoute>(AlertRoute.class) {};

  /**
   * Creates AlertRoute between the alert definition and the alert receiver. Fails if the receiver
   * or the definition are not found, or if the definition customer UUID doesn't match the passed
   * customer UUID.
   *
   * @param customerUUID
   * @param definitionUUID
   * @param receiverUUID
   * @return
   */
  public static AlertRoute create(UUID customerUUID, UUID definitionUUID, UUID receiverUUID) {
    AlertRoute route = new AlertRoute();
    route.uuid = UUID.randomUUID();
    route.definitionUUID = definitionUUID;
    route.receiverUUID = receiverUUID;
    route.save();
    return route;
  }

  public UUID getUuid() {
    return uuid;
  }

  public void setUUID(UUID uuid) {
    this.uuid = uuid;
  }

  public UUID getDefinitionUUID() {
    return definitionUUID;
  }

  public void setDefinitionUUID(UUID definitionUUID) {
    this.definitionUUID = definitionUUID;
  }

  public UUID getReceiverUUID() {
    return receiverUUID;
  }

  public void setReceiverUUID(UUID receiverUUID) {
    this.receiverUUID = receiverUUID;
  }

  @Override
  public int hashCode() {
    return Objects.hash(definitionUUID, receiverUUID, uuid);
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }
    if (!(obj instanceof AlertRoute)) {
      return false;
    }
    AlertRoute other = (AlertRoute) obj;
    return Objects.equals(definitionUUID, other.definitionUUID)
        && Objects.equals(receiverUUID, other.receiverUUID)
        && Objects.equals(uuid, other.uuid);
  }

  @Override
  public String toString() {
    return "AlertRoute [uuid="
        + uuid
        + ", definitionUUID="
        + definitionUUID
        + ", receiverUUID="
        + receiverUUID
        + "]";
  }

  public static AlertRoute get(UUID routeUUID) {
    return find.query().where().idEq(routeUUID).findOne();
  }

  public static AlertRoute getOrBadRequest(UUID routeUUID) {
    AlertRoute alertRoute = get(routeUUID);
    if (alertRoute == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Route UUID: " + routeUUID);
    }
    return alertRoute;
  }

  public static List<AlertRoute> listByDefinition(UUID definitionUUID) {
    return find.query().where().eq("definition_uuid", definitionUUID).findList();
  }

  public static List<AlertRoute> listByReceiver(UUID receiverUUID) {
    return find.query().where().eq("receiver_uuid", receiverUUID).findList();
  }

  public static List<AlertRoute> listByCustomer(UUID customerUUID) {
    Finder<UUID, AlertDefinition> definitionsFinder =
        new Finder<UUID, AlertDefinition>(AlertDefinition.class) {};
    List<AlertDefinition> definitions =
        definitionsFinder.query().where().eq("customer_uuid", customerUUID).findList();
    List<AlertRoute> routes = new ArrayList<>();
    definitions.forEach(
        definition -> routes.addAll(AlertRoute.listByDefinition(definition.getUuid())));
    return routes;
  }
}
