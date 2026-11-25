/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import com.google.inject.Inject;
import java.util.Arrays;
import java.util.Base64;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import javax.inject.Singleton;

@Singleton
public class EncryptionAtRestUniverseKeyCache {
  private static class EncryptionAtRestUniverseKeyCacheEntry {
    private String keyRef;
    private String keyVal;

    public EncryptionAtRestUniverseKeyCacheEntry(byte[] keyRef, byte[] keyVal) {
      this.keyRef = Base64.getEncoder().encodeToString(keyRef);
      this.keyVal = Base64.getEncoder().encodeToString(keyVal);
    }

    public byte[] getKeyRef() {
      return Base64.getDecoder().decode(this.keyRef);
    }

    public byte[] getKeyVal() {
      return Base64.getDecoder().decode(this.keyVal);
    }
  }

  private Map<UUID, EncryptionAtRestUniverseKeyCacheEntry> cache;

  @Inject
  public EncryptionAtRestUniverseKeyCache() {
    // Tasks on different universes read and write it at the same time.
    this.cache = new ConcurrentHashMap<>();
  }

  public void setCacheEntry(UUID universeUUID, byte[] keyRef, byte[] keyVal) {
    this.cache.put(universeUUID, new EncryptionAtRestUniverseKeyCacheEntry(keyRef, keyVal));
  }

  public byte[] getCacheEntry(UUID universeUUID, byte[] keyRef) {
    byte[] result = null;
    EncryptionAtRestUniverseKeyCacheEntry cacheEntry = this.cache.get(universeUUID);
    if (cacheEntry != null && Arrays.equals(cacheEntry.getKeyRef(), keyRef)) {
      result = cacheEntry.getKeyVal();
    }
    return result;
  }

  public void removeCacheEntry(UUID universeUUID) {
    this.cache.remove(universeUUID);
  }

  public void clearCache() {
    this.cache.clear();
  }
}
