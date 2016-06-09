// Copyright (c) Yugabyte, Inc.

package models;

import javax.persistence.*;
import com.avaje.ebean.Model;
import org.joda.time.DateTime;
import play.data.validation.Constraints;
import org.mindrot.jbcrypt.BCrypt;
import java.util.Date;
import java.util.UUID;

@Entity
public class Customer extends Model {
  @Id
  @GeneratedValue(strategy = GenerationType.IDENTITY)
  public long id;

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

  public static Finder<Long, Customer> find = new Finder(Long.class, Customer.class);

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
}
