// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.databind.JsonNode;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import java.util.List;
import java.util.UUID;

@Entity
public class AccessKey extends Model {
    public static class KeyInfo {
        public String publicKey;
        public String privateKey;
        public String accessSecret;
        public String accessKey;
    }

    @EmbeddedId
    @Constraints.Required
    public AccessKeyId idKey;

    @JsonBackReference
    public String getKeyCode() { return this.idKey.keyCode; }
    @JsonBackReference
    public UUID getProviderUUID() { return this.idKey.providerUUID; }

    @Constraints.Required
    @Column(nullable = false)
    @DbJson
    @JsonBackReference
    public JsonNode keyInfo;

    public void setKeyInfo(KeyInfo info) { this.keyInfo = Json.toJson(info); }
    public KeyInfo getKeyInfo() { return Json.fromJson(this.keyInfo, KeyInfo.class); }

    public static AccessKey create(UUID providerUUID, String key_code, KeyInfo key_info) {
        AccessKey accessKey = new AccessKey();
        accessKey.idKey = AccessKeyId.create(providerUUID, key_code);
        accessKey.setKeyInfo(key_info);
        accessKey.save();
        return accessKey;
    }

    private static final Find<AccessKeyId, AccessKey> find =
            new Find<AccessKeyId, AccessKey>() {};

    public static AccessKey get(UUID providerUUID, String keyCode) {
        return find.byId(AccessKeyId.create(providerUUID, keyCode));
    }

    public static List<AccessKey> getAll(UUID providerUUID) {
        return find.where().eq("provider_uuid", providerUUID).findList();
    }
}
