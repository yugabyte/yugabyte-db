package io.ebean;

public class MockHelper {
  public static Database mock(Database server, boolean defaultServer) {
    return DB.mock(server.name(), server, defaultServer);
  }
}
