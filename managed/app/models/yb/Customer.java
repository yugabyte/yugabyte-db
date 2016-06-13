// Copyright (c) Yugabyte, Inc.

package models.yb;

import javax.persistence.*;
import com.avaje.ebean.Model;
import com.fasterxml.jackson.databind.JsonNode;
import org.joda.time.DateTime;
import play.api.libs.json.Json;
import play.data.validation.Constraints;
import org.mindrot.jbcrypt.BCrypt;
import java.util.Date;
import java.util.Set;
import java.util.UUID;

@Entity
public class Customer extends Model {
  @Id
	public UUID uuid;

	@Column(length = 256, unique = true, nullable = false)
  @Constraints.Required
  @Constraints.Email
  private String email;
  public String getEmail() { return this.email; }

  @Column(length = 256, nullable = false)
  public String passwordHash;

  @Column(length = 256, nullable = false)
  @Constraints.Required
  @Constraints.MinLength(3)
  public String name;

  @Column(nullable = false)
  public Date creationDate;

  private String authToken;

  @Column(nullable = true)
  public DateTime authTokenIssueDate;

	@OneToMany(cascade = CascadeType.ALL)
	private Set<Instance> instances;
	public void setInstances(Set<Instance> instances) { this.instances = instances; }
	public Set<Instance> getInstances() { return this.instances; }

	public static final Find<Long, Customer> find = new Find<Long, Customer>(){};

  public Customer() {
      this.creationDate = new Date();
  }

	/**
   * Create new customer, we encrypt the password before we store it in the DB
   *
   * @param name
   * @param email
   * @param password
   * @return Newly Created Customer
   */
  public static Customer create(String name, String email, String password) {
    Customer cust = new Customer();
	  cust.uuid = UUID.randomUUID();
    cust.email = email.toLowerCase();
    cust.passwordHash = BCrypt.hashpw(password, BCrypt.gensalt());
    cust.name = name;
    cust.creationDate = new Date();

    cust.save();
    return cust;
  }

	/**
   * Validate if the email and password combination is valid, we use this to authenticate
   * the customer.
   * @param email
   * @param password
   * @return Authenticated Customer Info
   */
  public static Customer authWithPassword(String email, String password) {
    Customer cust = Customer.find.where().eq("email", email).findUnique();

    if (cust != null && BCrypt.checkpw(password, cust.passwordHash)) {
      return cust;
    } else {
      return null;
    }
  }

	/**
	 * Create a random auth token for the customer and store it in the DB.
   * @return authToken
   */
  public String createAuthToken() {
    authToken = UUID.randomUUID().toString();
    authTokenIssueDate = DateTime.now();
    save();
    return authToken;
  }

	/**
   * Authenicate with Token, would check if the authToken is valid.
   * @param authToken
   * @return Authenticated Customer Info
   */
  public static Customer authWithToken(String authToken) {
    if (authToken == null) {
      return null;
    }

    try {
      // TODO: handle authToken expiry etc.
      return find.where().eq("authToken", authToken).findUnique();
    } catch (Exception e) {
	    return null;
    }
  }

	/**
   * Delete the authToken for the customer.
   */
  public void deleteAuthToken() {
    authToken = null;
    authTokenIssueDate = null;
    save();
  }

	/**
	 * Create a new Yuga Instance for the customer
	 * @param name
	 * @param state
	 * @param placementInfo
	 * @return Yuga Instance
	 */
	public Instance createNewInstance(String name, Instance.State state, JsonNode placementInfo) {
		return Instance.create(this, name, state, placementInfo);
	}
}
