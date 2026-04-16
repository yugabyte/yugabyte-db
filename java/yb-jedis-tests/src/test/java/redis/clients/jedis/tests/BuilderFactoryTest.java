package redis.clients.jedis.tests;

import static org.yb.AssertionWrappers.assertEquals;

import org.junit.Test;

import redis.clients.jedis.BuilderFactory;

import org.junit.runner.RunWith;


import redis.clients.jedis.tests.BaseYBClassForJedis;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class BuilderFactoryTest extends BaseYBClassForJedis {
  @Test
  public void buildDouble() {
    Double build = BuilderFactory.DOUBLE.build("1.0".getBytes());
    assertEquals(new Double(1.0), build);
  }
}
