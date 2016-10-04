#include <cpp_redis/cpp_redis>

#include <signal.h>
#include <iostream>

volatile std::atomic_bool should_exit(false);
cpp_redis::redis_client client;

void
sigint_handler(int) {
  std::cout << "disconnected (sigint handler)" << std::endl;
  client.disconnect();
  should_exit = true;
}

int
main(void) {
  client.connect("127.0.0.1", 6379, [] (cpp_redis::redis_client&) {
    std::cout << "client disconnected (disconnection handler)" << std::endl;
    should_exit = true;
  });

  // same as client.send({ "SET", "hello", "42" }, ...)
  client.set("hello", "42", [] (cpp_redis::reply& reply) {
    std::cout << reply.as_string() << std::endl;
  });
  // same as client.send({ "DECRBY", "hello", 12 }, ...)
  client.decrby("hello", 12, [] (cpp_redis::reply& reply) {
    std::cout << reply.as_integer() << std::endl;
  });
  // same as client.send({ "GET", "hello" }, ...)
  client.get("hello", [] (cpp_redis::reply& reply) {
    std::cout << reply.as_string() << std::endl;
  });
  // commands are pipelined and only sent when client.commit() is called
  client.commit();
  // synchronous commit, no timeout
  // client.sync_commit();
  // synchronous commit, timeout
  // client.sync_commit(std::chrono::milliseconds(100));

  signal(SIGINT, &sigint_handler);
  while (not should_exit);

  return 0;
}
