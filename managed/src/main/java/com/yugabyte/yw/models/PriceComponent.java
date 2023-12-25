// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.ProviderAndRegion;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Junction;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
@Getter
@Setter
public class PriceComponent extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(PriceComponent.class);

  @EmbeddedId @Constraints.Required private PriceComponentKey idKey;

  // ManyToOne for provider is kept outside of PriceComponentKey
  // as ebean currently doesn't support having @ManyToOne inside @EmbeddedId
  // insertable and updatable are set to false as actual updates
  // are taken care by providerUuid parameter in PriceComponentKey
  @ManyToOne(optional = false)
  @JoinColumn(name = "provider_uuid", insertable = false, updatable = false)
  private Provider provider;

  public PriceComponentKey getIdKey() {
    return idKey;
  }

  @JsonIgnore
  public Provider getProvider() {
    if (this.provider == null) {
      setProviderUuid(this.idKey.providerUuid);
    }
    return this.provider;
  }

  @JsonIgnore
  public void setProvider(Provider aProvider) {
    provider = aProvider;
    idKey.providerUuid = aProvider.getUuid();
  }

  @JsonIgnore
  public UUID getProviderUuid() {
    return this.idKey.providerUuid;
  }

  @JsonIgnore
  public void setProviderUuid(UUID providerUuid) {
    Provider provider = Provider.get(providerUuid);
    if (provider != null) {
      setProvider(provider);
    } else {
      LOG.error("No provider found for the given id: {}", providerUuid);
    }
  }

  @JsonIgnore
  public String getProviderCode() {
    Provider provider = getProvider();
    return provider != null ? provider.getCode() : null;
  }

  public String getRegionCode() {
    return this.idKey.regionCode;
  }

  public String getComponentCode() {
    return this.idKey.componentCode;
  }

  @Column(name = "price_details_json")
  @DbJson
  private PriceDetails priceDetails = new PriceDetails();

  private static final Finder<PriceComponentKey, PriceComponent> find =
      new Finder<PriceComponentKey, PriceComponent>(PriceComponent.class) {};

  /**
   * Get a single specified pricing component for a given provider and region.
   *
   * @param providerUuid The cloud provider that the pricing component is in.
   * @param regionCode The region that the pricing component is in.
   * @param componentCode The pricing component's code.
   * @return The uniquely matching pricing component.
   */
  public static PriceComponent get(UUID providerUuid, String regionCode, String componentCode) {
    PriceComponentKey pcKey = PriceComponentKey.create(providerUuid, regionCode, componentCode);
    return PriceComponent.find.byId(pcKey);
  }

  /**
   * Query helper to find pricing components for a given cloud provider.
   *
   * @param provider The cloud provider to find pricing components of.
   * @return A list of pricing components in the cloud provider.
   */
  public static List<PriceComponent> findByProvider(Provider provider) {
    return PriceComponent.find.query().where().eq("provider_uuid", provider.getUuid()).findList();
  }

  public static List<PriceComponent> findByProvidersAndRegions(Collection<ProviderAndRegion> keys) {
    if (CollectionUtils.isEmpty(keys)) {
      return Collections.emptyList();
    }
    Set<ProviderAndRegion> uniqueKeys = new HashSet<>(keys);
    ExpressionList<PriceComponent> query = find.query().where();
    Junction<PriceComponent> orExpr = query.or();
    for (ProviderAndRegion key : uniqueKeys) {
      Junction<PriceComponent> andExpr = orExpr.and();
      andExpr.eq("provider_uuid", key.getProviderUuid());
      andExpr.eq("region_code", key.getRegionCode());
      orExpr.endAnd();
    }
    return new ArrayList<>(query.endOr().findList());
  }

  /**
   * Create or update a pricing component.
   *
   * @param providerUuid Cloud provider that the pricing component belongs to.
   * @param regionCode Region in the cloud provider that the pricing component belongs to.
   * @param componentCode The identifying code for the pricing component. Must be unique within the
   *     region.
   * @param priceDetails The pricing details of the component.
   * @return The newly created/updated pricing component.
   */
  public static PriceComponent upsert(
      UUID providerUuid, String regionCode, String componentCode, PriceDetails priceDetails) {
    PriceComponent component = PriceComponent.get(providerUuid, regionCode, componentCode);
    if (component == null) {
      component = new PriceComponent();
      component.idKey = PriceComponentKey.create(providerUuid, regionCode, componentCode);
    }
    PriceDetails details = priceDetails == null ? new PriceDetails() : priceDetails;
    component.setPriceDetails(details);
    component.save();
    return component;
  }

  /** The actual details of the pricing component. */
  public static class PriceDetails {

    // The unit on which the 'pricePerUnit' is based.
    public enum Unit {
      Hours,
      GBMonth,
      PIOPMonth,
      GiBpsMonth
    }

    // The price currency. Note that the case here matters as it matches AWS output.
    public enum Currency {
      USD
    }

    // The unit.
    public Unit unit;

    // Price per unit.
    public double pricePerUnit;

    // Price per hour. Derived from unit (might be per hour or per month).
    public double pricePerHour;

    // Price per day (24 hour day). Derived from unit (might be per hour or per month).
    public double pricePerDay;

    // Price per month (30 day month). Derived from unit (might be per hour or per month).
    public double pricePerMonth;

    // Currency.
    public Currency currency;

    // Keeping these around for now as they seem useful.
    public String effectiveDate;
    public String description;

    public void setUnitFromString(String unit) {
      switch (unit.toUpperCase()) {
        case "GB-MO":
        case "GBMONTH":
          this.unit = Unit.GBMonth;
          break;
        case "HRS":
        case "HOURS":
          this.unit = Unit.Hours;
          break;
        case "IOPS-MO":
          this.unit = Unit.PIOPMonth;
          break;
        case "GIBPS-MO":
          this.unit = Unit.GiBpsMonth;
          break;
        default:
          LOG.error("Invalid price unit provided: " + unit);
          break;
      }
    }
  }
}
