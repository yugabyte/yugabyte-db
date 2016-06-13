// Copyright (c) Yugabyte, Inc.

package models.yb;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.databind.JsonNode;
import org.joda.time.DateTime;
import play.data.validation.Constraints;
import javax.persistence.*;
import java.util.UUID;

@Entity
public class Instance extends Model {

	public enum State {
		@EnumValue("CRE")
		Created,

		@EnumValue("PRV")
		Provision,

		@EnumValue("RUN")
		Running,

		@EnumValue("DRP")
		Drop,

		@EnumValue("SHT")
		Shutdown,

		@EnumValue("UNK")
		Unknown
	}

	@EmbeddedId
	private CustomerInstanceKey key;
	public UUID getInstanceId() { return this.key.instanceId; }
	public UUID getCustomerId() { return this.key.customerId; }

	@Constraints.Required
	@Column(nullable = false)
	public String name;

	@Constraints.Required
	@ManyToOne
	public Customer customer;

	@Constraints.Required
	@Column(nullable = false)
	@DbJson
	JsonNode placementInfo;
	public JsonNode getPlacementInfo() { return this.placementInfo; }

	@Constraints.Required
	@Column(nullable = false)
	public State state;

	@Constraints.Required
	@Column(nullable = false)
	public DateTime creationDate;

	/**
	 * Query Helper for Instance with primary key
	 */
	public static final Find<Integer, Instance> find = new Find<Integer, Instance>(){};

	/**
	 * Create a new Yuga Instance
	 *
	 * @param customer
	 * @param name
	 * @param state
	 * @param placementInfo
	 * @return Yuga Instance
	 */
	public static Instance create(Customer customer, String name, State state, JsonNode placementInfo) {
		Instance instance = new Instance();
		instance.key = CustomerInstanceKey.create(UUID.randomUUID(), customer.uuid);
		instance.customer = customer;
		instance.name = name;
		instance.state = state;
		instance.placementInfo = placementInfo;
		instance.creationDate = DateTime.now();
		instance.save();
		return instance;
	}
}

