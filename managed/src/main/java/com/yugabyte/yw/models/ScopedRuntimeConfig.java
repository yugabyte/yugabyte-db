package com.yugabyte.yw.models;

import com.google.common.annotations.VisibleForTesting;
import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * This bean is a parent for ConfigEntries bean. Intent is to identify the scope of the child
 * ConfigEntries. This bean just points to one of many parent tables through a foreign key. At
 * present we are only adding such a pointer to provider and customer tables, so that we can have
 * scoped configurations for provider or customer.
 */
@Entity
@Getter
@Setter
public class ScopedRuntimeConfig extends Model {
  // TODO: delete if not needed by the end of it.
  //  public static final ScopedRuntimeConfig GLOBAL_SCOPE = new ScopedRuntimeConfig();
  public static final UUID GLOBAL_SCOPE_UUID = new UUID(0, 0);

  private static final Logger LOG = LoggerFactory.getLogger(ScopedRuntimeConfig.class);

  // This is not a foreign key but for ease of debugging
  // we  will reuse the uuid of parent entity. If there is
  // no uuid defined for parent entity we will
  // create a new uuid. At present all the use cases
  // global, provider, universe and customer have a uuid.
  @Id private final UUID uuid;

  // ====================================================
  // Foreign keys to scoping entities.
  // At most one of these can be valid.
  private final UUID customerUUID;

  private final UUID universeUUID;

  private final UUID providerUUID;

  // End foreign key fields
  // ====================================================

  private ScopedRuntimeConfig() {
    uuid = GLOBAL_SCOPE_UUID;
    customerUUID = null;
    universeUUID = null;
    providerUUID = null;
    LOG.info("Created Global ScopedRuntimeConfig");
  }

  private ScopedRuntimeConfig(Customer customer) {
    uuid = customer.getUuid();
    customerUUID = customer.getUuid();
    universeUUID = null;
    providerUUID = null;
    LOG.info("Created Customer({}) ScopedRuntimeConfig", getCustomerUUID());
  }

  private ScopedRuntimeConfig(Universe universe) {
    uuid = universe.getUniverseUUID();
    providerUUID = null;
    customerUUID = null;
    universeUUID = universe.getUniverseUUID();
    LOG.info("Created Universe({}) ScopedRuntimeConfig", getUniverseUUID());
  }

  private ScopedRuntimeConfig(Provider provider) {
    uuid = provider.getUuid();
    customerUUID = null;
    universeUUID = null;
    providerUUID = provider.getUuid();
    LOG.info("Created Provider({}) ScopedRuntimeConfig", getProviderUUID());
  }

  private static final Finder<UUID, ScopedRuntimeConfig> finder =
      new Finder<UUID, ScopedRuntimeConfig>(ScopedRuntimeConfig.class) {};

  @VisibleForTesting
  static ScopedRuntimeConfig get(UUID uuid) {
    return finder.byId(uuid);
  }

  // Below methods only used from RuntimeConfigEntry bean to ensure that scope is created
  // before creating the bean.
  static void ensureGlobal() {
    if (get(GLOBAL_SCOPE_UUID) == null) {
      new ScopedRuntimeConfig().save();
    }
  }

  static void ensure(Customer customer) {
    if (get(customer.getUuid()) == null) {
      new ScopedRuntimeConfig(customer).save();
    }
  }

  static void ensure(Universe universe) {
    if (get(universe.getUniverseUUID()) == null) {
      new ScopedRuntimeConfig(universe).save();
    }
  }

  static void ensure(Provider provider) {
    if (get(provider.getUuid()) == null) {
      new ScopedRuntimeConfig(provider).save();
    }
  }

  @Override
  public String toString() {
    if (GLOBAL_SCOPE_UUID.equals(getUuid())) {
      return "ScopedRuntimeConfig(GLOBAL_SCOPE)";
    } else {
      return "ScopedRuntimeConfig{"
          + "uuid="
          + getUuid()
          + ", customerUUID="
          + getCustomerUUID()
          + ", universeUUID="
          + getUniverseUUID()
          + ", providerUUID="
          + getProviderUUID()
          + '}';
    }
  }
}
