// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIdentityInfo;
import com.fasterxml.jackson.annotation.ObjectIdGenerators;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import lombok.Getter;
import lombok.Setter;

@JsonIdentityInfo(generator = ObjectIdGenerators.PropertyGenerator.class, property = "uuid")
@Entity
@ApiModel(description = "A custom hook's scope")
@Getter
@Setter
public class HookScope extends Model {

  public enum TriggerType {
    @EnumValue("PreNodeProvision")
    PreNodeProvision,

    @EnumValue("PostNodeProvision")
    PostNodeProvision,

    @EnumValue("ApiTriggered")
    ApiTriggered,

    /*
     * Hooks for Upgrade Tasks.
     * If the upgrade task is FooBar, then the hooks should be:
     * 1. PreFooBar (Before entire task)
     * 2. PostFooBar (After entire task)
     * 3. PreFooBarNodeUpgrade (Before node upgrade)
     * 4. PostFooBarNodeUpgrade (After node upgrade)
     */
    @EnumValue("PreRestartUniverseNodeUpgrade")
    PreRestartUniverseNodeUpgrade,

    @EnumValue("PostRestartUniverseNodeUpgrade")
    PostRestartUniverseNodeUpgrade,

    @EnumValue("PreRestartUniverse")
    PreRestartUniverse,

    @EnumValue("PostRestartUniverse")
    PostRestartUniverse,

    @EnumValue("PreSoftwareUpgradeNodeUpgrade")
    PreSoftwareUpgradeNodeUpgrade,

    @EnumValue("PostSoftwareUpgradeNodeUpgrade")
    PostSoftwareUpgradeNodeUpgrade,

    @EnumValue("PreSoftwareUpgrade")
    PreSoftwareUpgrade,

    @EnumValue("PostSoftwareUpgrade")
    PostSoftwareUpgrade,

    @EnumValue("PreRebootUniverseNodeUpgrade")
    PreRebootUniverseNodeUpgrade,

    @EnumValue("PostRebootUniverseNodeUpgrade")
    PostRebootUniverseNodeUpgrade,

    @EnumValue("PreRebootUniverse")
    PreRebootUniverse,

    @EnumValue("PostRebootUniverse")
    PostRebootUniverse,

    @EnumValue("PreThirdpartySoftwareUpgradeNodeUpgrade")
    PreThirdpartySoftwareUpgradeNodeUpgrade,

    @EnumValue("PostThirdpartySoftwareUpgradeNodeUpgrade")
    PostThirdpartySoftwareUpgradeNodeUpgrade,

    @EnumValue("PreThirdpartySoftwareUpgrade")
    PreThirdpartySoftwareUpgrade,

    @EnumValue("PostThirdpartySoftwareUpgrade")
    PostThirdpartySoftwareUpgrade;

    public static Optional<TriggerType> maybeResolve(String triggerName) {
      for (TriggerType triggerType : TriggerType.values()) {
        if (triggerType.name().equals(triggerName)) return Optional.of(triggerType);
      }
      return Optional.empty();
    }
  };

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Hook scope UUID", accessMode = READ_ONLY)
  private UUID uuid = UUID.randomUUID();

  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @Column(nullable = false)
  @ApiModelProperty(value = "Trigger", accessMode = READ_ONLY)
  private TriggerType triggerType;

  @Column(nullable = true)
  private UUID universeUUID;

  @Column(nullable = true)
  private UUID providerUUID;

  @Column(nullable = true)
  private UUID clusterUUID;

  @OneToMany private Set<Hook> hooks;

  public void addHook(Hook hook) {
    hook.setHookScope(this);
    hook.update();
  }

  public static HookScope create(UUID customerUUID, TriggerType triggerType) {
    HookScope hookScope = new HookScope();
    hookScope.setCustomerUUID(customerUUID);
    hookScope.setTriggerType(triggerType);
    hookScope.setUniverseUUID(null);
    hookScope.setProviderUUID(null);
    hookScope.save();
    return hookScope;
  }

  public static HookScope create(UUID customerUUID, TriggerType triggerType, Provider provider) {
    HookScope hookScope = new HookScope();
    hookScope.setCustomerUUID(customerUUID);
    hookScope.setTriggerType(triggerType);
    hookScope.setUniverseUUID(null);
    hookScope.setProviderUUID(provider.getUuid());
    hookScope.save();
    return hookScope;
  }

  public static HookScope create(
      UUID customerUUID, TriggerType triggerType, Universe universe, UUID clusterUUID) {
    HookScope hookScope = new HookScope();
    hookScope.setCustomerUUID(customerUUID);
    hookScope.setTriggerType(triggerType);
    hookScope.setUniverseUUID(universe.getUniverseUUID());
    hookScope.setProviderUUID(null);
    hookScope.setClusterUUID(clusterUUID);
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
      UUID customerUUID,
      TriggerType triggerType,
      UUID universeUUID,
      UUID providerUUID,
      UUID clusterUUID) {
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
    if (clusterUUID == null) findExpression = findExpression.isNull("cluster_uuid");
    else findExpression = findExpression.eq("cluster_uuid", clusterUUID);
    return findExpression.findOne();
  }
}
