package com.yugabyte.yw.models.migrations;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

public class V298 {

  @Entity
  @Table(name = "customer")
  public static class Customer extends Model {
    public static final Finder<UUID, Customer> find = new Finder<UUID, Customer>(Customer.class) {};

    public UUID uuid;

    @Id public Long id;

    public static List<Customer> getAll() {
      return find.query().findList();
    }
  }

  @Entity
  @Table(name = "universe")
  public static class Universe extends Model {

    public static final Finder<UUID, Universe> find = new Finder<UUID, Universe>(Universe.class) {};

    @Id public UUID universeUUID;

    public String name;

    @Column(columnDefinition = "TEXT", nullable = false)
    private String universeDetailsJson;

    public JsonNode getUniverseDetails() {
      try {
        ObjectMapper objectMapper = new ObjectMapper();
        return objectMapper.readTree(universeDetailsJson);
      } catch (JsonProcessingException e) {
        return null;
      }
    }

    public void setUniverseDetails(String details) {
      this.universeDetailsJson = details;
      super.save();
    }

    public static Set<Universe> getAllFromCustomer(Customer customer) {
      List<Universe> rawList = find.query().where().eq("customer_id", customer.id).findList();
      return rawList.stream().collect(Collectors.toSet());
    }
  }
}
