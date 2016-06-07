// Copyright (c) Yugabyte, Inc.

package models;

import org.junit.Test;
import org.mindrot.jbcrypt.BCrypt;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;
import play.test.WithApplication;

import javax.persistence.PersistenceException;
import java.util.List;
import java.util.Map;
import static org.junit.Assert.*;

public class CustomerTest extends WithApplication {
    @Override
    protected Application provideApplication() {
        return new GuiceApplicationBuilder()
                .configure((Map) Helpers.inMemoryDatabase())
                .build();
    }

    @Test
    public void testCreate() {
        Customer customer = Customer.create("Test Customer", "foo@bar.com", "password");
        customer.save();
        assertNotNull(customer.id);
        assertEquals("foo@bar.com", customer.getEmail());
        assertEquals("Test Customer", customer.name);
        assertNotNull(customer.creationDate);
        assertTrue(BCrypt.checkpw("password", customer.passwordHash));
    }

    @Test(expected = PersistenceException.class)
    public void testCreateWithDuplicateEmail() {
        Customer c1 = Customer.create("C1", "foo@foo.com", "password");
        c1.save();
        Customer c2 = Customer.create("C2", "foo@foo.com", "password");
        c2.save();
    }

    @Test
    public void findAll() {
        Customer c1 = Customer.create("C1", "foo@foo.com", "password");
        c1.save();
        Customer c2 = Customer.create("C2", "bar@foo.com", "password");
        c2.save();

        List<Customer> customerList = Customer.find.all();

        assertEquals(2, customerList.size());
    }

    @Test
    public void authenticateWithEmailAndValidPassword() {
        Customer c = Customer.create("C1", "foo@foo.com", "password");
        c.save();
        Customer authCust = Customer.authWithPassword("foo@foo.com", "password");
        assertEquals(authCust.id, c.id);
    }

    @Test
    public void authenticateWithEmailAndInvalidPassword() {
        Customer c = Customer.create("C1", "foo@foo.com", "password");
        c.save();
        Customer authCust = Customer.authWithPassword("foo@foo.com", "password1");
        assertNull(authCust);
    }

    @Test
    public void testCreateAuthToken() {
        Customer c = Customer.create("C1", "foo@foo.com", "password");
        c.save();

        assertNotNull(c.id);

        String authToken = c.createAuthToken();
        assertNotNull(authToken);
        assertNotNull(c.authTokenIssueDate);

        Customer authCust = Customer.authWithToken(authToken);
        assertEquals(authCust.id, c.id);
    }

    @Test
    public void testDeleteAuthToken() {
        Customer c = Customer.create("C1", "foo@foo.com", "password");
        c.save();

        assertNotNull(c.id);

        String authToken = c.createAuthToken();
        assertNotNull(authToken);
        assertNotNull(c.authTokenIssueDate);

        Customer fetchCust = Customer.find.byId(c.id);
        fetchCust.deleteAuthToken();

        fetchCust = Customer.find.byId(c.id);
        assertNull(fetchCust.authTokenIssueDate);

        Customer authCust = Customer.authWithToken(authToken);
        assertNull(authCust);
    }

}
