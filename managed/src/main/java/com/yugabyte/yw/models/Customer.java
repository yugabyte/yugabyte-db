// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.deepMerge;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.Transient;
import java.util.Collection;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

@Entity
@ApiModel(description = "Customer information, including associated universes")
@Getter
@Setter
public class Customer extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(Customer.class);

  // A globally unique UUID for the customer.
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID uuid = UUID.randomUUID();

  // An auto incrementing, user-friendly id for the customer. Used to compose a db prefix. Currently
  // it is assumed that there is a single instance of the db. The id space for this field may have
  // to be partitioned in case the db is being sharded.
  // Use IDENTITY strategy because `customer.id` is a `bigserial` type; not a sequence.
  @Id
  @GeneratedValue(strategy = GenerationType.IDENTITY)
  @ApiModelProperty(value = "Customer ID", accessMode = READ_ONLY, example = "1")
  @JsonProperty("customerId")
  private Long id;

  @Column(length = 15, nullable = false)
  @Constraints.Required
  @ApiModelProperty(value = "Customer code", example = "admin", required = true)
  private String code;

  @Column(length = 256, nullable = false)
  @Constraints.Required
  @Constraints.MinLength(3)
  @ApiModelProperty(value = "Name of customer", example = "sridhar", required = true)
  private String name;

  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation time",
      example = "2022-12-12T13:07:18Z",
      accessMode = READ_ONLY)
  private Date creationDate;

  // To be replaced with runtime config
  @Column(nullable = true, columnDefinition = "TEXT")
  @ApiModelProperty(value = "UI_ONLY", hidden = true, accessMode = READ_ONLY)
  private JsonNode features;

  @Transient
  @JsonProperty
  @ApiModelProperty(value = "Universe UUIDs", hidden = true, accessMode = READ_ONLY)
  // Used for API response to calls made internally by cloud for resource tracking.
  private Set<UUID> universeUuids;

  // This sets the transient field which is used only for API response.
  @JsonIgnore
  public void setTransientUniverseUUIDs(Set<UUID> universeUuids) {
    this.universeUuids = universeUuids;
  }

  @JsonIgnore
  public Set<UUID> getUniverseUUIDs() {
    return Universe.getUniverseUUIDsForCustomer(getId());
  }

  @JsonIgnore
  public Set<Universe> getUniverses() {
    return Universe.getUniversesForCustomer(getId());
  }

  @JsonIgnore
  public Set<Universe> getUniversesForProvider(UUID providerUUID) {
    Set<Universe> universesInProvider =
        getUniverses().stream()
            .filter(u -> checkClusterInProvider(u, providerUUID))
            .collect(Collectors.toSet());
    return universesInProvider;
  }

  private boolean checkClusterInProvider(Universe universe, UUID providerUUID) {
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      if (cluster.userIntent.provider.equals(providerUUID.toString())) {
        return true;
      }
    }
    return false;
  }

  public static final Finder<UUID, Customer> find = new Finder<UUID, Customer>(Customer.class) {};

  public static Customer getOrBadRequest(UUID customerUUID) {
    Customer customer = get(customerUUID);
    if (customer == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Customer UUID:" + customerUUID);
    }
    return customer;
  }

  public static Customer get(UUID customerUUID) {
    return find.query().where().eq("uuid", customerUUID).findOne();
  }

  public static List<Customer> getForUpdate(Collection<UUID> customerUUIDs) {
    return find.query().forUpdate().where().in("uuid", customerUUIDs).findList();
  }

  public static Customer get(long id) {
    return find.query().where().idEq(String.valueOf(id)).findOne();
  }

  public static List<Customer> getAll() {
    return find.query().findList();
  }

  public Customer() {
    this.setCreationDate(new Date());
  }

  /** Create new customer, we encrypt the password before we store it in the DB */
  public static Customer create(String code, String name) {
    Customer cust = new Customer();
    cust.setCode(code);
    cust.setName(name);
    cust.setCreationDate(new Date());
    cust.save();
    return cust;
  }

  /** Get features for this customer. */
  public JsonNode getFeatures() {
    return features == null ? Json.newObject() : features;
  }

  /**
   * Upserts features for this customer. If updating a feature, only specified features will be
   * updated.
   */
  public void upsertFeatures(JsonNode input) {
    if (!input.isObject()) {
      throw new PlatformServiceException(BAD_REQUEST, "Features must be Jsons.");
    } else if (features == null || features.isNull() || features.size() == 0) {
      features = input;
    } else {
      deepMerge(features, input);
    }
    save();
  }

  @JsonIgnore
  public String getTag() {
    return String.format("[%s][%s]", getName(), getCode());
  }
}
