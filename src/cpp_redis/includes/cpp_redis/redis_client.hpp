#pragma once

#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <functional>
#include <condition_variable>

#include "cpp_redis/network/redis_connection.hpp"

namespace cpp_redis {

class redis_client {
public:
  //! ctor & dtor
  redis_client(void);
  ~redis_client(void);

  //! copy ctor & assignment operator
  redis_client(const redis_client&) = delete;
  redis_client& operator=(const redis_client&) = delete;

public:
  //! handle connection
  typedef std::function<void(redis_client&)> disconnection_handler_t;
  void connect(const std::string& host = "127.0.0.1", unsigned int port = 6379,
               const disconnection_handler_t& disconnection_handler = nullptr);
  void disconnect(void);
  bool is_connected(void);

  //! send cmd
  typedef std::function<void(reply&)> reply_callback_t;
  redis_client& send(const std::vector<std::string>& redis_cmd, const reply_callback_t& callback = nullptr);

  //! commit pipelined transaction
  redis_client& commit(void);
  redis_client& sync_commit(void);

  template<class Rep, class Period>
  redis_client& sync_commit(const std::chrono::duration<Rep, Period>& timeout) {
    try_commit();

    std::unique_lock<std::mutex> lock_callback(m_callbacks_mutex);
    m_sync_condvar.wait_for(lock_callback, timeout, [=]{ return m_callbacks_running == 0 and m_callbacks.empty(); });

    return *this;
  }

public:
  redis_client& append(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& auth(const std::string& password, const reply_callback_t& reply_callback = nullptr);
  redis_client& bgrewriteaof(const reply_callback_t& reply_callback = nullptr);
  redis_client& bgsave(const reply_callback_t& reply_callback = nullptr);
  redis_client& bitcount(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& bitcount(const std::string& key, int start, int end, const reply_callback_t& reply_callback = nullptr);
  // redis_client& bitfield(const std::string& key, const reply_callback_t& reply_callback = nullptr) key [get type offset] [set type offset value] [incrby type offset increment] [overflow wrap|sat|fail]
  redis_client& bitop(const std::string& operation, const std::string& destkey, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& bitpos(const std::string& key, int bit, const reply_callback_t& reply_callback = nullptr);
  redis_client& bitpos(const std::string& key, int bit, int start, const reply_callback_t& reply_callback = nullptr);
  redis_client& bitpos(const std::string& key, int bit, int start, int end, const reply_callback_t& reply_callback = nullptr);
  redis_client& blpop(const std::vector<std::string>& keys, int timeout, const reply_callback_t& reply_callback = nullptr);
  redis_client& brpop(const std::vector<std::string>& keys, int timeout, const reply_callback_t& reply_callback = nullptr);
  redis_client& brpoplpush(const std::string& src, const std::string& dst, int timeout, const reply_callback_t& reply_callback = nullptr);
  // redis_client& client_kill(const reply_callback_t& reply_callback = nullptr) [ip:port] [id client-id] [type normal|master|slave|pubsub] [addr ip:port] [skipme yes/no]
  redis_client& client_list(const reply_callback_t& reply_callback = nullptr);
  redis_client& client_getname(const reply_callback_t& reply_callback = nullptr);
  redis_client& client_pause(int timeout, const reply_callback_t& reply_callback = nullptr);
  redis_client& client_reply(const std::string& mode, const reply_callback_t& reply_callback = nullptr);
  redis_client& client_setname(const std::string& name, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_addslots(const std::vector<std::string>& slots, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_count_failure_reports(const std::string& node_id, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_countkeysinslot(const std::string& slot, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_delslots(const std::vector<std::string>& slots, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_failover(const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_failover(const std::string& mode, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_forget(const std::string& node_id, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_getkeysinslot(const std::string& slot, int count, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_info(const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_keyslot(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_meet(const std::string& ip, int port, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_nodes(const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_replicate(const std::string& node_id, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_reset(const std::string& mode = "soft", const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_saveconfig(const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_set_config_epoch(const std::string& epoch, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_setslot(const std::string& slot, const std::string& mode, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_setslot(const std::string& slot, const std::string& mode, const std::string& node_id, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_slaves(const std::string& node_id, const reply_callback_t& reply_callback = nullptr);
  redis_client& cluster_slots(const reply_callback_t& reply_callback = nullptr);
  redis_client& command(const reply_callback_t& reply_callback = nullptr);
  redis_client& command_count(const reply_callback_t& reply_callback = nullptr);
  redis_client& command_getkeys(const reply_callback_t& reply_callback = nullptr);
  redis_client& command_info(const std::vector<std::string>& command_name, const reply_callback_t& reply_callback = nullptr);
  redis_client& config_get(const std::string& param, const reply_callback_t& reply_callback = nullptr);
  redis_client& config_rewrite(const reply_callback_t& reply_callback = nullptr);
  redis_client& config_set(const std::string& param, const std::string& val, const reply_callback_t& reply_callback = nullptr);
  redis_client& config_resetstat(const reply_callback_t& reply_callback = nullptr);
  redis_client& dbsize(const reply_callback_t& reply_callback = nullptr);
  redis_client& debug_object(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& debug_segfault(const reply_callback_t& reply_callback = nullptr);
  redis_client& decr(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& decrby(const std::string& key, int val, const reply_callback_t& reply_callback = nullptr);
  redis_client& del(const std::vector<std::string>& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& discard(const reply_callback_t& reply_callback = nullptr);
  redis_client& dump(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& echo(const std::string& msg, const reply_callback_t& reply_callback = nullptr);
  redis_client& eval(const std::string& script, int numkeys, const std::vector<std::string>& keys, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr);
  redis_client& evalsha(const std::string& sha1, int numkeys, const std::vector<std::string>& keys, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr);
  redis_client& exec(const reply_callback_t& reply_callback = nullptr);
  redis_client& exists(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& expire(const std::string& key, int seconds, const reply_callback_t& reply_callback = nullptr);
  redis_client& expireat(const std::string& key, int timestamp, const reply_callback_t& reply_callback = nullptr);
  redis_client& flushall(const reply_callback_t& reply_callback = nullptr);
  redis_client& flushdb(const reply_callback_t& reply_callback = nullptr);
  redis_client& geoadd(const std::string& key, const std::vector<std::tuple<std::string, std::string, std::string>>& long_lat_memb, const reply_callback_t& reply_callback = nullptr);
  redis_client& geohash(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr);
  redis_client& geopos(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr);
  redis_client& geodist(const std::string& key, const std::string& member_1, const std::string& member_2, const std::string& unit = "m", const reply_callback_t& reply_callback = nullptr);
  // redis_client& georadius(const reply_callback_t& reply_callback = nullptr) key longitude latitude radius m|km|ft|mi [withcoord] [withdist] [withhash] [count count] [asc|desc] [store key] [storedist key]
  // redis_client& georadiusbymember(const reply_callback_t& reply_callback = nullptr) key member radius m|km|ft|mi [withcoord] [withdist] [withhash] [count count] [asc|desc] [store key] [storedist key]
  redis_client& get(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& getbit(const std::string& key, int offset, const reply_callback_t& reply_callback = nullptr);
  redis_client& getrange(const std::string& key, int start, int end, const reply_callback_t& reply_callback = nullptr);
  redis_client& getset(const std::string& key, const std::string& val, const reply_callback_t& reply_callback = nullptr);
  redis_client& hdel(const std::string& key, const std::vector<std::string>& fields, const reply_callback_t& reply_callback = nullptr);
  redis_client& hexists(const std::string& key, const std::string& field, const reply_callback_t& reply_callback = nullptr);
  redis_client& hget(const std::string& key, const std::string& field, const reply_callback_t& reply_callback = nullptr);
  redis_client& hgetall(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& hincrby(const std::string& key, const std::string& field, int incr, const reply_callback_t& reply_callback = nullptr);
  redis_client& hincrbyfloat(const std::string& key, const std::string& field, float incr, const reply_callback_t& reply_callback = nullptr);
  redis_client& hkeys(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& hlen(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& hmget(const std::string& key, const std::vector<std::string>& fields, const reply_callback_t& reply_callback = nullptr);
  redis_client& hmset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& field_val, const reply_callback_t& reply_callback = nullptr);
  redis_client& hset(const std::string& key, const std::string& field, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& hsetnx(const std::string& key, const std::string& field, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& hstrlen(const std::string& key, const std::string& field, const reply_callback_t& reply_callback = nullptr);
  redis_client& hvals(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& incr(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& incrby(const std::string& key, int incr, const reply_callback_t& reply_callback = nullptr);
  redis_client& incrbyfloat(const std::string& key, float incr, const reply_callback_t& reply_callback = nullptr);
  redis_client& info(const std::string& section = "default", const reply_callback_t& reply_callback = nullptr);
  redis_client& keys(const std::string& pattern, const reply_callback_t& reply_callback = nullptr);
  redis_client& lastsave(const reply_callback_t& reply_callback = nullptr);
  redis_client& lindex(const std::string& key, int index, const reply_callback_t& reply_callback = nullptr);
  redis_client& linsert(const std::string& key, const std::string& before_after, const std::string& pivot, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& llen(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& lpop(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& lpush(const std::string& key, const std::vector<std::string>& values, const reply_callback_t& reply_callback = nullptr);
  redis_client& lpushx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& lrange(const std::string& key, int start, int stop, const reply_callback_t& reply_callback = nullptr);
  redis_client& lrem(const std::string& key, int count, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& lset(const std::string& key, int index, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& ltrim(const std::string& key, int start, int stop, const reply_callback_t& reply_callback = nullptr);
  redis_client& mget(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& migrate(const std::string& host, int port, const std::string& key, const std::string& dest_db, int timeout, bool copy = false, bool replace = false, const std::vector<std::string>& keys = {}, const reply_callback_t& reply_callback = nullptr);
  redis_client& monitor(const reply_callback_t& reply_callback = nullptr);
  redis_client& move(const std::string& key, const std::string& db, const reply_callback_t& reply_callback = nullptr);
  redis_client& mset(const std::vector<std::pair<std::string, std::string>>& key_vals, const reply_callback_t& reply_callback = nullptr);
  redis_client& msetnx(const std::vector<std::pair<std::string, std::string>>& key_vals, const reply_callback_t& reply_callback = nullptr);
  redis_client& multi(const reply_callback_t& reply_callback = nullptr);
  redis_client& object(const std::string& subcommand, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr);
  redis_client& persist(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& pexpire(const std::string& key, int milliseconds, const reply_callback_t& reply_callback = nullptr);
  redis_client& pexpireat(const std::string& key, int milliseconds_timestamp, const reply_callback_t& reply_callback = nullptr);
  redis_client& pfadd(const std::string& key, const std::vector<std::string>& elements, const reply_callback_t& reply_callback = nullptr);
  redis_client& pfcount(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& pfmerge(const std::string& destkey, const std::vector<std::string>& sourcekeys, const reply_callback_t& reply_callback = nullptr);
  redis_client& ping(const reply_callback_t& reply_callback = nullptr);
  redis_client& ping(const std::string& message, const reply_callback_t& reply_callback = nullptr);
  redis_client& psetex(const std::string& key, int milliseconds, const std::string& val, const reply_callback_t& reply_callback = nullptr);
  redis_client& pubsub(const std::string& subcommand, const std::vector<std::string>& args, const reply_callback_t& reply_callback = nullptr);
  redis_client& pttl(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& quit(const reply_callback_t& reply_callback = nullptr);
  redis_client& randomkey(const reply_callback_t& reply_callback = nullptr);
  redis_client& readonly(const reply_callback_t& reply_callback = nullptr);
  redis_client& readwrite(const reply_callback_t& reply_callback = nullptr);
  redis_client& rename(const std::string& key, const std::string& newkey, const reply_callback_t& reply_callback = nullptr);
  redis_client& renamenx(const std::string& key, const std::string& newkey, const reply_callback_t& reply_callback = nullptr);
  redis_client& restore(const std::string& key, int ttl, const std::string& serialized_value, const reply_callback_t& reply_callback = nullptr);
  redis_client& restore(const std::string& key, int ttl, const std::string& serialized_value, const std::string& replace, const reply_callback_t& reply_callback = nullptr);
  redis_client& role(const reply_callback_t& reply_callback = nullptr);
  redis_client& rpop(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& rpoplpush(const std::string& source, const std::string& destination, const reply_callback_t& reply_callback = nullptr);
  redis_client& rpush(const std::string& key, const std::vector<std::string>& values, const reply_callback_t& reply_callback = nullptr);
  redis_client& rpushx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& sadd(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr);
  redis_client& save(const reply_callback_t& reply_callback = nullptr);
  redis_client& scard(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& script_debug(const std::string& mode, const reply_callback_t& reply_callback = nullptr);
  redis_client& script_exists(const std::vector<std::string>& scripts, const reply_callback_t& reply_callback = nullptr);
  redis_client& script_flush(const reply_callback_t& reply_callback = nullptr);
  redis_client& script_kill(const reply_callback_t& reply_callback = nullptr);
  redis_client& script_load(const std::string& script, const reply_callback_t& reply_callback = nullptr);
  redis_client& sdiff(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& sdiffstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& select(int index, const reply_callback_t& reply_callback = nullptr);
  redis_client& set(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& set_advanced(const std::string& key, const std::string& value, bool ex = false, int ex_sec = 0, bool px = false, int px_milli = 0, bool nx = false, bool xx = false, const reply_callback_t& reply_callback = nullptr);
  redis_client& setbit(const std::string& key, int offset, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& setex(const std::string& key, int seconds, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& setnx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback = nullptr) ;
  redis_client& setrange(const std::string& key, int offset, const std::string& value, const reply_callback_t& reply_callback = nullptr);
  redis_client& shutdown(const reply_callback_t& reply_callback = nullptr);
  redis_client& shutdown(const std::string& save, const reply_callback_t& reply_callback = nullptr);
  redis_client& sinter(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& sinterstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& sismember(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr);
  redis_client& slaveof(const std::string& host, int port, const reply_callback_t& reply_callback = nullptr);
  redis_client& slowlog(const std::string subcommand, const reply_callback_t& reply_callback = nullptr);
  redis_client& slowlog(const std::string subcommand, const std::string& argument, const reply_callback_t& reply_callback = nullptr);
  redis_client& smembers(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& smove(const std::string& source, const std::string& destination, const std::string& member, const reply_callback_t& reply_callback = nullptr);
  // redis_client& sort(const reply_callback_t& reply_callback = nullptr) key [by pattern] [limit offset count] [get pattern [get pattern ...]] [asc|desc] [alpha] [store destination]
  redis_client& spop(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& spop(const std::string& key, int count, const reply_callback_t& reply_callback = nullptr);
  redis_client& srandmember(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& srandmember(const std::string& key, int count, const reply_callback_t& reply_callback = nullptr);
  redis_client& srem(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr);
  redis_client& strlen(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& sunion(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& sunionstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  redis_client& sync(const reply_callback_t& reply_callback = nullptr);
  redis_client& time(const reply_callback_t& reply_callback = nullptr);
  redis_client& ttl(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& type(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& unwatch(const reply_callback_t& reply_callback = nullptr);
  redis_client& wait(int numslaves, int timeout, const reply_callback_t& reply_callback = nullptr);
  redis_client& watch(const std::vector<std::string>& keys, const reply_callback_t& reply_callback = nullptr);
  // redis_client& zadd(const reply_callback_t& reply_callback = nullptr) key [nx|xx] [ch] [incr] score member [score member ...]
  redis_client& zcard(const std::string& key, const reply_callback_t& reply_callback = nullptr);
  redis_client& zcount(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr);
  redis_client& zincrby(const std::string& key, int incr, const std::string& member, const reply_callback_t& reply_callback = nullptr);
  // redis_client& zinterstore(const reply_callback_t& reply_callback = nullptr) destination numkeys key [key ...] [weights weight [weight ...]] [aggregate sum|min|max]
  redis_client& zlexcount(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr);
  redis_client& zrange(const std::string& key, int start, int stop, bool withscores = false, const reply_callback_t& reply_callback = nullptr);
  // redis_client& zrangebylex(const reply_callback_t& reply_callback = nullptr) key min max [limit offset count]
  // redis_client& zrevrangebylex(const reply_callback_t& reply_callback = nullptr) key max min [limit offset count]
  // redis_client& zrangebyscore(const reply_callback_t& reply_callback = nullptr) key min max [withscores] [limit offset count]
  redis_client& zrank(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr);
  redis_client& zrem(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback = nullptr);
  redis_client& zremrangebylex(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr);
  redis_client& zremrangebyrank(const std::string& key, int start, int stop, const reply_callback_t& reply_callback = nullptr);
  redis_client& zremrangebyscore(const std::string& key, int min, int max, const reply_callback_t& reply_callback = nullptr);
  redis_client& zrevrange(const std::string& key, int start, int stop, bool withscores = false, const reply_callback_t& reply_callback = nullptr);
  // redis_client& zrevrangebyscore(const reply_callback_t& reply_callback = nullptr) key max min [withscores] [limit offset count]
  redis_client& zrevrank(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr);
  redis_client& zscore(const std::string& key, const std::string& member, const reply_callback_t& reply_callback = nullptr);
  // redis_client& zunionstore(const reply_callback_t& reply_callback = nullptr) destination numkeys key [key ...] [weights weight [weight ...]] [aggregate sum|min|max]
  // redis_client& scan(const reply_callback_t& reply_callback = nullptr) cursor [match pattern] [count count]
  // redis_client& sscan(const reply_callback_t& reply_callback = nullptr) key cursor [match pattern] [count count]
  // redis_client& hscan(const reply_callback_t& reply_callback = nullptr) key cursor [match pattern] [count count]
  // redis_client& zscan(const reply_callback_t& reply_callback = nullptr) key cursor [match pattern] [count count]

private:
  //! receive & disconnection handlers
  void connection_receive_handler(network::redis_connection&, reply& reply);
  void connection_disconnection_handler(network::redis_connection&);

  void clear_callbacks(void);
  void call_disconnection_handler(void);

  void try_commit(void);

private:
  //! tcp client for redis connection
  network::redis_connection m_client;

  //! queue of callback to process
  std::queue<reply_callback_t> m_callbacks;

  //! user defined disconnection handler
  disconnection_handler_t m_disconnection_handler;

  //! thread safety
  std::mutex m_callbacks_mutex;
  std::mutex m_send_mutex;
  std::condition_variable m_sync_condvar;
  std::atomic_uint m_callbacks_running;
};

} //! cpp_redis
