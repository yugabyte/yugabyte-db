// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIdentityInfo;
import com.fasterxml.jackson.annotation.ObjectIdGenerators;
import io.ebean.Model;
import io.ebean.Finder;
import io.ebean.ExpressionList;
import io.ebean.annotation.EnumValue;
import java.util.UUID;
import java.util.Set;
import java.util.List;
import javax.persistence.Entity;
import javax.persistence.Column;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@JsonIdentityInfo(generator = ObjectIdGenerators.PropertyGenerator.class, property = "uuid")
@Entity
@ApiModel(description = "A custom hook's scope")
public class HookScope extends Model {

  public enum TriggerType {
    @EnumValue("PreNodeProvision")
    PreNodeProvision,

    @EnumValue("PostNodeProvision")
    PostNodeProvision,

    @EnumValue("PreNodeUpgrade")
    PreNodeUpgrade,

    @EnumValue("PostNodeUpgrade")
    PostNodeUpgrade,

    @EnumValue("PreUpgradeUniverse")
    PreUpgradeUniverse,

    @EnumValue("PostUpgradeUniverse")
    PostUpgradeUniverse,

    @EnumValue("ApiTriggered")
    ApiTriggered;
  };

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Hook scope UUID", accessMode = READ_ONLY)
  public UUID uuid = UUID.randomUUID();

  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  public UUID customerUUID;

  @Column(nullable = false)
  @ApiModelProperty(value = "Trigger", accessMode = READ_ONLY)
  public TriggerType triggerType;

  @Column(nullable = true)
  public UUID universeUUID;

  @Column(nullable = true)
  public UUID providerUUID;

  @OneToMany private Set<Hook> hooks;

  public Set<Hook> getHooks() {
    return hooks;
  }

  public static HookScope create(UUID customerUUID, TriggerType triggerType) {
    HookScope hookScope = new HookScope();
    hookScope.customerUUID = customerUUID;
    hookScope.triggerType = triggerType;
    hookScope.universeUUID = null;
    hookScope.providerUUID = null;
    hookScope.save();
    return hookScope;
  }

  public static HookScope create(UUID customerUUID, TriggerType triggerType, Provider provider) {
    HookScope hookScope = new HookScope();
    hookScope.customerUUID = customerUUID;
    hookScope.triggerType = triggerType;
    hookScope.universeUUID = null;
    hookScope.providerUUID = provider.uuid;
    hookScope.save();
    return hookScope;
  }

  public static HookScope create(UUID customerUUID, TriggerType triggerType, Universe universe) {
    HookScope hookScope = new HookScope();
    hookScope.customerUUID = customerUUID;
    hookScope.triggerType = triggerType;
    hookScope.universeUUID = universe.universeUUID;
    hookScope.providerUUID = null;
    hookScope.save();
    return hookScope;
  }

  public static final Finder<UUID, HookScope> find =
      new Finder<UUID, HookScope>(HookScope.class) {};

  public static HookScope getOrBadRequest(UUID customerUUID, UUID hookScopeUUID) {
    HookScope hookScope =
        find.query().where().eq("customer_uuid", customerUUID).eq("uuid", hookScopeUUID).findOne();
    if (hookScope == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid HookScope UUID:" + hookScopeUUID);
    }
    return hookScope;
  }

  public static List<HookScope> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static HookScope getByTriggerScopeId(
      UUID customerUUID, TriggerType triggerType, UUID universeUUID, UUID providerUUID) {
    if (universeUUID != null && providerUUID != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "At most one of universe UUID and provider UUID can be null");
    }
    ExpressionList<HookScope> findExpression =
        find.query().where().eq("customer_uuid", customerUUID).eq("trigger_type", triggerType);
    if (providerUUID == null) findExpression = findExpression.isNull("provider_uuid");
    else findExpression = findExpression.eq("provider_uuid", providerUUID);
    if (universeUUID == null) findExpression = findExpression.isNull("universe_uuid");
    else findExpression = findExpression.eq("universe_uuid", universeUUID);
    return findExpression.findOne();
  }
}
