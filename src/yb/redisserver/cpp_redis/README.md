<p align="center">
   <img src="https://raw.githubusercontent.com/Cylix/cpp_redis/master/assets/images/cpp_redis_logo.jpg"/>
</p>

# cpp_redis
cpp_redis is C++11 Asynchronous Redis Client (with support for synchronous operations).

Network is based on raw sockets API, making the library really lightweight.
Commands pipelining is supported.

## Requirements
* C++11
* Mac or Linux (no support for Windows platforms)

## Compiling
The library uses `cmake`. In order to build the library, follow these steps:

```bash
git clone https://github.com/Cylix/cpp_redis.git
cd cpp_redis
./install_deps.sh # necessary only for building tests
mkdir build
cd build
cmake .. # only library
cmake .. -DBUILD_TESTS=true # library and tests
cmake .. -DBUILD_EXAMPLES=true # library and examples
cmake .. -DBUILD_TESTS=true -DBUILD_EXAMPLES=true # library, tests and examples
cmake .. -DCPP_REDIS_READ_SIZE=4096 # Change the read size used to read data from sockets (default: 4096)
make -j
```

If you want to install the library in a specific folder:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/destination/path ..
make install -j
```

Then, you just have to include `<cpp_redis/cpp_redis>` in your source files and link the `cpp_redis` library with your project.

To build the tests, it is necessary to install `google_tests`. Just run the `install_deps.sh` script which does the work for you.

## Redis Client
`redis_client` is the class providing communication with a redis server.

### Methods

#### void connect(const std::string& host = "127.0.0.1", unsigned int port = 6379, const disconnection_handler& handler = nullptr)
Connect to the Redis Server. Connection is done synchronously.
Throws redis_error in case of failure or if client if already connected.

Also set the disconnection handler which is called whenever a disconnection has occurred.
Disconnection handler is an `std::function<void(redis_client&)>`.

#### void disconnect(void)
Disconnect client from remote host.
Throws redis_error if client is not connected to any server.

#### bool is_connected(void)
Returns whether the client is connected or not.

#### redis_client& send(const std::vector<std::string>& redis_cmd, const reply_callback& callback = nullptr)
Send a command and set the callback which has to be called when the reply has been received.
If `nullptr` is passed as callback, command is executed and no callback will be called.
Reply callback is an `std::function<void(reply&)>`.

The command is not effectively sent immediately, but stored inside an internal buffer until `commit()` is called.

#### redis_client& commit(void)
Send all the commands that have been stored by calling `send()` since the last `commit()` call to the redis server.

That is, pipelining is supported in a very simple and efficient way: `client.send(...).send(...).send(...).commit()` will send the 3 commands at once (instead of sending 3 network requests, one for each command, as it would have been done without pipelining).

`commit()` works asynchronously: it returns immediately after sending the queued requests and replies are processed asynchronously.

Another version of `commit()`, `sync_commit()`, provides a synchronous version of the commit function. It sends all the requests and wait that all replies are received before returning.

`sync_commit()` also support timeout, using `std::chrono`: `client.sync_commit(std::chrono::milliseconds(100));`.

### Commands
The following list contains all the supported commands. If a command is missing or that the method to call that command is not convenient, please use the `send()` method directly.

All the following member functions are wrappers for the `send()` function to provide more code clarity.

```cpp
redis_client& append(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& auth(const std::string& password, const reply_callback_t& reply_callback = nullptr)
redis_client& bgrewriteaof(const reply_callback_t& reply_callback = nullptr)
redis_client& bgsave(const reply_callback_t& reply_callback = nullptr)
redis_client& bitcount(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& bitcount(const std::string& key, int start, int end, const reply_callback_t& reply_callback = nullptr)
redis_client& bitop(const std::string& operation, const std::string& destkey, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& bitpos(const std::string& key, int bit, const reply_callback_t& reply_callback = nullptr)
redis_client& bitpos(const std::string& key, int bit, int start, const reply_callback_t& reply_callback = nullptr)
redis_client& bitpos(const std::string& key, int bit, int start, int end, const reply_callback_t& reply_callback = nullptr)
redis_client& blpop(const std::vector<std::string>& keys, int timeout, const reply_callback_t& reply_callback = nullptr)
redis_client& brpop(const std::vector<std::string>& keys, int timeout, const reply_callback_t& reply_callback = nullptr)
redis_client& brpoplpush(const std::string& src, const std::string& dst, int timeout, const reply_callback_t& reply_callback = nullptr)
redis_client& client_list(const reply_callback_t& reply_callback = nullptr)
redis_client& client_getname(const reply_callback_t& reply_callback = nullptr)
redis_client& client_pause(int timeout, const reply_callback_t& reply_callback = nullptr)
redis_client& client_reply(const std::string& mode, const reply_callback_t& reply_callback = nullptr)
redis_client& client_setname(const std::string& name, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_addslots(const std::vector<std::string>& slots, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_count_failure_reports(const std::string& node_id, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_countkeysinslot(const std::string& slot, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_delslots(const std::vector<std::string>& slots, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_failover(const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_failover(const std::string& mode, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_forget(const std::string& node_id, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_getkeysinslot(const std::string& slot, int count, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_info(const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_keyslot(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_meet(const std::string& ip, int port, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_nodes(const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_replicate(const std::string& node_id, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_reset(const std::string& mode = "soft", const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_saveconfig(const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_set_config_epoch(const std::string& epoch, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_setslot(const std::string& slot, const std::string& mode, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_setslot(const std::string& slot, const std::string& mode, const std::string& node_id, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_slaves(const std::string& node_id, const reply_callback_t& reply_callback = nullptr)
redis_client& cluster_slots(const reply_callback_t& reply_callback = nullptr)
redis_client& command(const reply_callback_t& reply_callback = nullptr)
redis_client& command_count(const reply_callback_t& reply_callback = nullptr)
redis_client& command_getkeys(const reply_callback_t& reply_callback = nullptr)
redis_client& command_info(const std::vector<std::string>& command_name, const reply_callback_t& reply_callback = nullptr)
redis_client& config_get(const std::string& param, const reply_callback_t& reply_callback = nullptr)
redis_client& config_rewrite(const reply_callback_t& reply_callback = nullptr)
redis_client& config_set(const std::string& param, const std::string& val, const reply_callback_t& reply_callback = nullptr)
redis_client& config_resetstat(const reply_callback_t& reply_callback = nullptr)
redis_client& dbsize(const reply_callback_t& reply_callback = nullptr)
redis_client& debug_object(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& debug_segfault(const reply_callback_t& reply_callback = nullptr)
redis_client& decr(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& decrby(const std::string& key, int val, const reply_callback_t& reply_callback = nullptr)
redis_client& del(const std::vector<std::string>& key, const reply_callback_t& reply_callback = nullptr)
redis_client& discard(const reply_callback_t& reply_callback = nullptr)
redis_client& dump(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& echo(const std::string& msg, const reply_callback_t& reply_callback = nullptr)
redis_client& eval(const std::string& script, int numkeys, const std::vector<std::string>& keys, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr)
redis_client& evalsha(const std::string& sha1, int numkeys, const std::vector<std::string>& keys, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr)
redis_client& exec(const reply_callback_t& reply_callback = nullptr)
redis_client& exists(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& expire(const std::string& key, int seconds, const reply_callback_t& reply_callback = nullptr)
redis_client& expireat(const std::string& key, int timestamp, const reply_callback_t& reply_callback = nullptr)
redis_client& flushall(const reply_callback_t& reply_callback = nullptr)
redis_client& flushdb(const reply_callback_t& reply_callback = nullptr)
redis_client& geoadd(const std::string& key, const std::vector<std::tuple<std::string, std::string, std::string>>& long_lat_memb, const reply_callback_t& reply_callback = nullptr)
redis_client& geohash(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr)
redis_client& geopos(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr)
redis_client& geodist(const std::string& key, const std::string& member_1, const std::string& member_2, const std::string& unit = "m", const reply_callback_t& reply_callback = nullptr)
redis_client& get(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& getbit(const std::string& key, int offset, const reply_callback_t& reply_callback = nullptr)
redis_client& getrange(const std::string& key, int start, int end, const reply_callback_t& reply_callback = nullptr)
redis_client& getset(const std::string& key, const std::string& val, const reply_callback_t& reply_callback = nullptr)
redis_client& hdel(const std::string& key, const std::vector<std::string>& fields, const reply_callback_t& reply_callback = nullptr)
redis_client& hexists(const std::string& key, const std::string& field, const reply_callback_t& reply_callback = nullptr)
redis_client& hget(const std::string& key, const std::string& field, const reply_callback_t& reply_callback = nullptr)
redis_client& hgetall(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& hincrby(const std::string& key, const std::string& field, int incr, const reply_callback_t& reply_callback = nullptr)
redis_client& hincrbyfloat(const std::string& key, const std::string& field, float incr, const reply_callback_t& reply_callback = nullptr)
redis_client& hkeys(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& hlen(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& hmget(const std::string& key, const std::vector<std::string>& fields, const reply_callback_t& reply_callback = nullptr)
redis_client& hmset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& field_val, const reply_callback_t& reply_callback = nullptr)
redis_client& hset(const std::string& key, const std::string& field, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& hsetnx(const std::string& key, const std::string& field, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& hstrlen(const std::string& key, const std::string& field, const reply_callback_t& reply_callback = nullptr)
redis_client& hvals(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& incr(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& incrby(const std::string& key, int incr, const reply_callback_t& reply_callback = nullptr)
redis_client& incrbyfloat(const std::string& key, float incr, const reply_callback_t& reply_callback = nullptr)
redis_client& info(const std::string& section = "default", const reply_callback_t& reply_callback = nullptr)
redis_client& keys(const std::string& pattern, const reply_callback_t& reply_callback = nullptr)
redis_client& lastsave(const reply_callback_t& reply_callback = nullptr)
redis_client& lindex(const std::string& key, int index, const reply_callback_t& reply_callback = nullptr)
redis_client& linsert(const std::string& key, const std::string& before_after, const std::string& pivot, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& llen(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& lpop(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& lpush(const std::string& key, const std::vector<std::string>& values, const reply_callback_t& reply_callback = nullptr)
redis_client& lpushx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& lrange(const std::string& key, int start, int stop, const reply_callback_t& reply_callback = nullptr)
redis_client& lrem(const std::string& key, int count, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& lset(const std::string& key, int index, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& ltrim(const std::string& key, int start, int stop, const reply_callback_t& reply_callback = nullptr)
redis_client& mget(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& migrate(const std::string& host, int port, const std::string& key, const std::string& dest_db, int timeout, bool copy = false, bool replace = false, const std::vector<std::string>& keys = {}, const reply_callback_t& reply_callback = nullptr)
redis_client& monitor(const reply_callback_t& reply_callback = nullptr)
redis_client& move(const std::string& key, const std::string& db, const reply_callback_t& reply_callback = nullptr)
redis_client& mset(const std::vector<std::pair<std::string, std::string>>& key_vals, const reply_callback_t& reply_callback = nullptr)
redis_client& msetnx(const std::vector<std::pair<std::string, std::string>>& key_vals, const reply_callback_t& reply_callback = nullptr)
redis_client& multi(const reply_callback_t& reply_callback = nullptr)
redis_client& object(const std::string& subcommand, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr)
redis_client& persist(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& pexpire(const std::string& key, int milliseconds, const reply_callback_t& reply_callback = nullptr)
redis_client& pexpireat(const std::string& key, int milliseconds_timestamp, const reply_callback_t& reply_callback = nullptr)
redis_client& pfadd(const std::string& key, const std::vector<std::string>& elements, const reply_callback_t& reply_callback = nullptr)
redis_client& pfcount(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& pfmerge(const std::string& destkey, const std::vector<std::string>& sourcekeys, const reply_callback_t& reply_callback = nullptr)
redis_client& ping(const reply_callback_t& reply_callback = nullptr)
redis_client& ping(const std::string& message, const reply_callback_t& reply_callback = nullptr)
redis_client& psetex(const std::string& key, int milliseconds, const std::string& val, const reply_callback_t& reply_callback = nullptr)
redis_client& pubsub(const std::string& subcommand, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr)
redis_client& pttl(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& quit(const reply_callback_t& reply_callback = nullptr)
redis_client& randomkey(const reply_callback_t& reply_callback = nullptr)
redis_client& readonly(const reply_callback_t& reply_callback = nullptr)
redis_client& readwrite(const reply_callback_t& reply_callback = nullptr)
redis_client& rename(const std::string& key, const std::string& newkey, const reply_callback_t& reply_callback = nullptr)
redis_client& renamenx(const std::string& key, const std::string& newkey, const reply_callback_t& reply_callback = nullptr)
redis_client& restore(const std::string& key, int ttl, const std::string& serialized_value, const reply_callback_t& reply_callback = nullptr)
redis_client& restore(const std::string& key, int ttl, const std::string& serialized_value, const std::string& replace, const reply_callback_t& reply_callback = nullptr)
redis_client& role(const reply_callback_t& reply_callback = nullptr)
redis_client& rpop(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& rpoplpush(const std::string& source, const std::string& destination, const reply_callback_t& reply_callback = nullptr)
redis_client& rpush(const std::string& key, const std::vector<std::string>& values, const reply_callback_t& reply_callback = nullptr)
redis_client& rpushx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& sadd(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr)
redis_client& save(const reply_callback_t& reply_callback = nullptr)
redis_client& scard(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& script_debug(const std::string& mode, const reply_callback_t& reply_callback = nullptr)
redis_client& script_exists(const std::vector<std::string>& scripts, const reply_callback_t& reply_callback = nullptr)
redis_client& script_flush(const reply_callback_t& reply_callback = nullptr)
redis_client& script_kill(const reply_callback_t& reply_callback = nullptr)
redis_client& script_load(const std::string& script, const reply_callback_t& reply_callback = nullptr)
redis_client& sdiff(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& sdiffstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& select(int index, const reply_callback_t& reply_callback = nullptr)
redis_client& set(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& set_advanced(const std::string& key, const std::string& value, bool ex = false, int ex_sec = 0, bool px = false, int px_milli = 0, bool nx = false, bool xx = false, const reply_callback_t& reply_callback = nullptr)
redis_client& setbit(const std::string& key, int offset, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& setex(const std::string& key, int seconds, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& setnx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr) ;
redis_client& setrange(const std::string& key, int offset, const std::string& value, const reply_callback_t& reply_callback = nullptr)
redis_client& shutdown(const reply_callback_t& reply_callback = nullptr)
redis_client& shutdown(const std::string& save, const reply_callback_t& reply_callback = nullptr)
redis_client& sinter(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& sinterstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& sismember(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr)
redis_client& slaveof(const std::string& host, int port, const reply_callback_t& reply_callback = nullptr)
redis_client& slowlog(const std::string subcommand, const reply_callback_t& reply_callback = nullptr)
redis_client& slowlog(const std::string subcommand, const std::string& argument, const reply_callback_t& reply_callback = nullptr)
redis_client& smembers(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& smove(const std::string& source, const std::string& destination, const std::string& member, const reply_callback_t& reply_callback = nullptr)
redis_client& spop(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& spop(const std::string& key, int count, const reply_callback_t& reply_callback = nullptr)
redis_client& srandmember(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& srandmember(const std::string& key, int count, const reply_callback_t& reply_callback = nullptr)
redis_client& srem(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr)
redis_client& strlen(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& sunion(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& sunionstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& sync(const reply_callback_t& reply_callback = nullptr)
redis_client& time(const reply_callback_t& reply_callback = nullptr)
redis_client& ttl(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& type(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& unwatch(const reply_callback_t& reply_callback = nullptr)
redis_client& wait(int numslaves, int timeout, const reply_callback_t& reply_callback = nullptr)
redis_client& watch(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr)
redis_client& zcard(const std::string& key, const reply_callback_t& reply_callback = nullptr)
redis_client& zcount(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr)
redis_client& zincrby(const std::string& key, int incr, const std::string& member, const reply_callback_t& reply_callback = nullptr)
redis_client& zlexcount(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr)
redis_client& zrange(const std::string& key, int start, int stop, bool withscores = false, const reply_callback_t& reply_callback = nullptr)
redis_client& zrank(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr)
redis_client& zrem(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr)
redis_client& zremrangebylex(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr)
redis_client& zremrangebyrank(const std::string& key, int start, int stop, const reply_callback_t& reply_callback = nullptr)
redis_client& zremrangebyscore(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr)
redis_client& zrevrange(const std::string& key, int start, int stop, bool withscores = false, const reply_callback_t& reply_callback = nullptr)
redis_client& zrevrank(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr)
redis_client& zscore(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr)
```

### Example

```cpp
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
```

## Redis Subscriber

### Methods

#### void connect(const std::string& host = "127.0.0.1", unsigned int port = 6379, const disconnection_handler& handler = nullptr)
Connect to the Redis Server. Connection is done synchronously.
Throws redis_error in case of failure or if client if already connected.

Also set the disconnection handler which is called whenever a disconnection has occurred.
Disconnection handler is an `std::function<void(redis_subscriber&)>`.

#### void disconnect(void)
Disconnect client from remote host.
Throws redis_error if client is not connected to any server.

#### bool is_connected(void)
Returns whether the client is connected or not.

#### redis_subscriber& subscribe(const std::string& channel, const subscribe_callback& callback)
Subscribe to the given channel and call subscribe_callback each time a message is published in this channel.
subscribe_callback is an `std::function<void(const std::string&, const std::string&)>`.

The command is not effectively sent immediately, but stored inside an internal buffer until `commit()` is called.

#### redis_subscriber& psubscribe(const std::string& pattern, const subscribe_callback& callback)
PSubscribe to the given pattern and call subscribe_callback each time a message is published in a channel matching the pattern.
subscribe_callback is an `std::function<void(const std::string&, const std::string&)>`.

The command is not effectively sent immediately, but stored inside an internal buffer until `commit()` is called.

#### redis_subscriber& unsubscribe(const std::string& channel)
Unsubscribe from the given channel.

The command is not effectively sent immediately, but stored inside an internal buffer until `commit()` is called.

#### redis_subscriber& punsubscribe(const std::string& pattern)
Unsubscribe from the given pattern.

The command is not effectively sent immediately, but stored inside an internal buffer until `commit()` is called.

#### redis_subscriber& commit(void)
Send all the commands that have been stored by calling `send()` since the last `commit()` call to the redis server.

That is, pipelining is supported in a very simple and efficient way: `sub.subscribe(...).psubscribe(...).unsubscribe(...).commit()` will send the 3 commands at once (instead of sending 3 network requests, one for each command, as it would have been done without pipelining).

### Example

```cpp
#include <cpp_redis/cpp_redis>

#include <signal.h>
#include <iostream>

volatile std::atomic_bool should_exit(false);
cpp_redis::redis_subscriber sub;

void
sigint_handler(int) {
  std::cout << "disconnected (sigint handler)" << std::endl;
  sub.disconnect();
  should_exit = true;
}

int
main(void) {
  sub.connect("127.0.0.1", 6379, [](cpp_redis::redis_subscriber&) {
    std::cout << "sub disconnected (disconnection handler)" << std::endl;
    should_exit = true;
  });

  sub.subscribe("some_chan", [] (const std::string& chan, const std::string& msg) {
    std::cout << "MESSAGE " << chan << ": " << msg << std::endl;
  });
  sub.psubscribe("*", [] (const std::string& chan, const std::string& msg) {
    std::cout << "PMESSAGE " << chan << ": " << msg << std::endl;
  });
  sub.commit();

  signal(SIGINT, &sigint_handler);
  while (not should_exit);

  return 0;
}
```

## Reply
`reply` is the class that wraps redis server replies. That is, `reply` objects are passed as parameters of commands callbacks and contain the server's response.

### Methods

#### bool is_array(void) const
Returns whether the reply is an array or not.

#### bool is_string(void) const
Returns whether the reply is a string (simple string or bulk string) or not.

#### bool is_simple_string(void) const
Returns whether the reply is a simple string or not.

#### bool is_bulk_string(void) const
Returns whether the reply is a bulk string or not.

#### bool is_error(void) const
Returns whether the reply is an error or not.

#### bool is_integer(void) const
Returns whether the reply is an integer or not.

#### bool is_null(void) const
Returns whether the reply is null or not.

#### const std::vector<reply>& as_array(void) const
Returns the reply as an array.
Behavior is undefined if the reply is not an array.

#### const std::string& as_string(void) const
Returns the reply as a string.
Behavior is undefined if the reply is not a string.

#### int as_integer(void) const
Returns the reply as an integer.
Behavior is undefined if the reply is not an integer.

#### type get_type(void) const
Return the type of the reply. The possible values are the following:

```cpp
enum class type {
  array,
  bulk_string,
  error,
  integer,
  simple_string,
  null
};
```

## Examples
Some examples are provided in this repository:
* [redis_client.cpp](examples/redis_client.cpp) shows how to use the redis client class.
* [redis_subscriber.cpp](examples/redis_subscriber.cpp) shows how to use the redis subscriber class.

## Special Thanks

* [Tobias Gustafsson](https://github.com/tobbe303) for contributing and reporting issues
* Alexis Vasseur for reporting me issues and spending time helping me debugging
* Pawel Lopko	for reporting me issues

## Author
[Simon Ninon](http://simon-ninon.fr)
