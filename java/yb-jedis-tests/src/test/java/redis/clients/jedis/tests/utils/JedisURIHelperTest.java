package redis.clients.jedis.tests.utils;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNull;

import java.net.URI;
import java.net.URISyntaxException;

import org.junit.Test;
import org.junit.Ignore;

import redis.clients.util.JedisURIHelper;

import org.junit.runner.RunWith;


import redis.clients.jedis.tests.BaseYBClassForJedis;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class JedisURIHelperTest extends BaseYBClassForJedis {

  @Test
  public void shouldGetPasswordFromURIWithCredentials() throws URISyntaxException {
    URI uri = new URI("redis://user:password@host:9000/0");
    assertEquals("password", JedisURIHelper.getPassword(uri));
  }

  @Test
  public void shouldReturnNullIfURIDoesNotHaveCredentials() throws URISyntaxException {
    URI uri = new URI("redis://host:9000/0");
    assertNull(JedisURIHelper.getPassword(uri));
  }

  @Test
  @Ignore public void shouldGetDbFromURIWithCredentials() throws URISyntaxException {
    URI uri = new URI("redis://user:password@host:9000/3");
    assertEquals(3, JedisURIHelper.getDBIndex(uri));
  }

  @Test
  @Ignore public void shouldGetDbFromURIWithoutCredentials() throws URISyntaxException {
    URI uri = new URI("redis://host:9000/4");
    assertEquals(4, JedisURIHelper.getDBIndex(uri));
  }

  @Test
  @Ignore public void shouldGetDefaultDbFromURIIfNoDbWasSpecified() throws URISyntaxException {
    URI uri = new URI("redis://host:9000");
    assertEquals(0, JedisURIHelper.getDBIndex(uri));
  }

  @Test
  public void shouldValidateInvalidURIs() throws URISyntaxException {
    assertFalse(JedisURIHelper.isValid(new URI("host:9000")));
    assertFalse(JedisURIHelper.isValid(new URI("user:password@host:9000/0")));
    assertFalse(JedisURIHelper.isValid(new URI("host:9000/0")));
    assertFalse(JedisURIHelper.isValid(new URI("redis://host/0")));
  }

}
