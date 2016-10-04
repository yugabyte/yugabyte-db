#include "cpp_redis/redis_client.hpp"
#include "cpp_redis/redis_error.hpp"

namespace cpp_redis {

redis_client::redis_client(void)
: m_callbacks_running(0)
{}

redis_client::~redis_client(void) {
  if (is_connected())
    disconnect();
}

void
redis_client::connect(const std::string& host, unsigned int port,
                      const disconnection_handler_t& client_disconnection_handler)
{
  m_disconnection_handler = client_disconnection_handler;

  auto disconnection_handler = std::bind(&redis_client::connection_disconnection_handler, this, std::placeholders::_1);
  auto receive_handler = std::bind(&redis_client::connection_receive_handler, this, std::placeholders::_1, std::placeholders::_2);
  m_client.connect(host, port, disconnection_handler, receive_handler);
}

void
redis_client::disconnect(void) {
  m_client.disconnect();
}

bool
redis_client::is_connected(void) {
  return m_client.is_connected();
}

redis_client&
redis_client::send(const std::vector<std::string>& redis_cmd, const reply_callback_t& callback) {
  std::lock_guard<std::mutex> lock_callback(m_callbacks_mutex);

  m_client.send(redis_cmd);
  m_callbacks.push(callback);

  return *this;
}

//! commit pipelined transaction
redis_client&
redis_client::commit(void) {
  try_commit();

  return *this;
}

redis_client&
redis_client::sync_commit(void) {
  try_commit();

  std::unique_lock<std::mutex> lock_callback(m_callbacks_mutex);
  m_sync_condvar.wait(lock_callback, [=]{ return m_callbacks_running == 0 and m_callbacks.empty(); });

  return *this;
}

void
redis_client::try_commit(void) {
  try {
    m_client.commit();
  }
  catch (const cpp_redis::redis_error& e) {
    clear_callbacks();
    throw e;
  }
}

void
redis_client::connection_receive_handler(network::redis_connection&, reply& reply) {
  reply_callback_t callback = nullptr;

  {
    std::lock_guard<std::mutex> lock(m_callbacks_mutex);

    if (m_callbacks.size()) {
      callback = m_callbacks.front();
      m_callbacks_running += 1;
      m_callbacks.pop();
    }
  }

  if (callback)
    callback(reply);

  m_callbacks_running -= 1;
  m_sync_condvar.notify_all();
}

void
redis_client::clear_callbacks(void) {
  std::lock_guard<std::mutex> lock(m_callbacks_mutex);

  std::queue<reply_callback_t> empty;
  std::swap(m_callbacks, empty);
}

void
redis_client::call_disconnection_handler(void) {
  if (m_disconnection_handler)
    m_disconnection_handler(*this);
}

void
redis_client::connection_disconnection_handler(network::redis_connection&) {
  clear_callbacks();
  call_disconnection_handler();
}


redis_client&
redis_client::append(const std::string& key, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "APPEND", key, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::auth(const std::string& password, const reply_callback_t& reply_callback) {
  send({ "AUTH", password }, reply_callback);
  return *this;
}

redis_client&
redis_client::bgrewriteaof(const reply_callback_t& reply_callback) {
  send({ "BGREWRITEAOF"}, reply_callback);
  return *this;
}

redis_client&
redis_client::bgsave(const reply_callback_t& reply_callback) {
  send({ "BGSAVE" }, reply_callback);
  return *this;
}

redis_client&
redis_client::bitcount(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "BITCOUNT", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::bitcount(const std::string& key, int start, int end, const reply_callback_t& reply_callback) {
  send({ "BITCOUNT", key, std::to_string(start), std::to_string(end) }, reply_callback);
  return *this;
}

redis_client&
redis_client::bitop(const std::string& operation, const std::string& destkey, const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "BITOP", operation, destkey };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::bitpos(const std::string& key, int bit, const reply_callback_t& reply_callback) {
  send({ "BITPOS", key, std::to_string(bit) }, reply_callback);
  return *this;
}

redis_client&
redis_client::bitpos(const std::string& key, int bit, int start, const reply_callback_t& reply_callback) {
  send({ "BITPOS", key, std::to_string(bit), std::to_string(start) }, reply_callback);
  return *this;
}

redis_client&
redis_client::bitpos(const std::string& key, int bit, int start, int end, const reply_callback_t& reply_callback) {
  send({ "BITPOS", key, std::to_string(bit), std::to_string(start), std::to_string(end) }, reply_callback);
  return *this;
}

redis_client&
redis_client::blpop(const std::vector<std::string>& keys, int timeout, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "BLPOP" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  cmd.push_back(std::to_string(timeout));
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::brpop(const std::vector<std::string>& keys, int timeout, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "BRPOP" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  cmd.push_back(std::to_string(timeout));
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::brpoplpush(const std::string& src, const std::string& dst, int timeout, const reply_callback_t& reply_callback) {
  send({ "BRPOPLPUSH", src, dst, std::to_string(timeout) }, reply_callback);
  return *this;
}

redis_client&
redis_client::client_list(const reply_callback_t& reply_callback) {
  send({ "CLIENT", "LIST" }, reply_callback);
  return *this;
}

redis_client&
redis_client::client_getname(const reply_callback_t& reply_callback) {
  send({ "CLIENT", "GETNAME" }, reply_callback);
  return *this;
}

redis_client&
redis_client::client_pause(int timeout, const reply_callback_t& reply_callback) {
  send({ "CLIENT", "PAUSE", std::to_string(timeout) }, reply_callback);
  return *this;
}

redis_client&
redis_client::client_reply(const std::string& mode, const reply_callback_t& reply_callback) {
  send({ "CLIENT", "REPLY", mode }, reply_callback);
  return *this;
}

redis_client&
redis_client::client_setname(const std::string& name, const reply_callback_t& reply_callback) {
  send({ "CLIENT", "SETNAME", name }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_addslots(const std::vector<std::string>& slots, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "CLUSTER", "ADDSLOTS" };
  cmd.insert(cmd.end(), slots.begin(), slots.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_count_failure_reports(const std::string& node_id, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "COUNT-FAILURE-REPORTS", node_id }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_countkeysinslot(const std::string& slot, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "COUNTKEYSINSLOT", slot }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_delslots(const std::vector<std::string>& slots, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "CLUSTER", "DELSLOTS" };
  cmd.insert(cmd.end(), slots.begin(), slots.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_failover(const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "FAILOVER" }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_failover(const std::string& mode, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "FAILOVER", mode }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_forget(const std::string& node_id, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "FORGET", node_id }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_getkeysinslot(const std::string& slot, int count, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "GETKEYSINSLOT", slot, std::to_string(count) }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_info(const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "INFO" }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_keyslot(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "KEYSLOT", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_meet(const std::string& ip, int port, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "MEET", ip, std::to_string(port) }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_nodes(const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "NODES" }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_replicate(const std::string& node_id, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "REPLICATE", node_id }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_reset(const std::string& mode, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "RESET", mode }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_saveconfig(const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "SAVECONFIG" }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_set_config_epoch(const std::string& epoch, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "SET-CONFIG-EPOCH", epoch }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_setslot(const std::string& slot, const std::string& mode, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "SETSLOT", slot, mode }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_setslot(const std::string& slot, const std::string& mode, const std::string& node_id, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "SETSLOT", slot, mode, node_id }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_slaves(const std::string& node_id, const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "SLAVES", node_id }, reply_callback);
  return *this;
}

redis_client&
redis_client::cluster_slots(const reply_callback_t& reply_callback) {
  send({ "CLUSTER", "SLOTS" }, reply_callback);
  return *this;
}

redis_client&
redis_client::command(const reply_callback_t& reply_callback) {
  send({ "COMMAND" }, reply_callback);
  return *this;
}

redis_client&
redis_client::command_count(const reply_callback_t& reply_callback) {
  send({ "COMMAND", "COUNT" }, reply_callback);
  return *this;
}

redis_client&
redis_client::command_getkeys(const reply_callback_t& reply_callback) {
  send({ "COMMAND", "GETKEYS" }, reply_callback);
  return *this;
}

redis_client&
redis_client::command_info(const std::vector<std::string>& command_name, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "COMMAND", "COUNT" };
  cmd.insert(cmd.end(), command_name.begin(), command_name.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::config_get(const std::string& param, const reply_callback_t& reply_callback) {
  send({ "CONFIG", "GET", param }, reply_callback);
  return *this;
}

redis_client&
redis_client::config_rewrite(const reply_callback_t& reply_callback) {
  send({ "CONFIG", "REWRITE" }, reply_callback);
  return *this;
}

redis_client&
redis_client::config_set(const std::string& param, const std::string& val, const reply_callback_t& reply_callback) {
  send({ "CONFIG", "SET", param, val }, reply_callback);
  return *this;
}

redis_client&
redis_client::config_resetstat(const reply_callback_t& reply_callback) {
  send({ "CONFIG", "RESETSTAT" }, reply_callback);
  return *this;
}

redis_client&
redis_client::dbsize(const reply_callback_t& reply_callback) {
  send({ "DBSIZE" }, reply_callback);
  return *this;
}

redis_client&
redis_client::debug_object(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "DEBUG", "OBJECT", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::debug_segfault(const reply_callback_t& reply_callback) {
  send({ "DEBUG", "SEGFAULT" }, reply_callback);
  return *this;
}

redis_client&
redis_client::decr(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "DECR", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::decrby(const std::string& key, int val, const reply_callback_t& reply_callback) {
  send({ "DECRBY", key, std::to_string(val) }, reply_callback);
  return *this;
}

redis_client&
redis_client::del(const std::vector<std::string>& key, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "DEL" };
  cmd.insert(cmd.end(), key.begin(), key.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::discard(const reply_callback_t& reply_callback) {
  send({ "DISCARD" }, reply_callback);
  return *this;
}

redis_client&
redis_client::dump(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "DUMP", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::echo(const std::string& msg, const reply_callback_t& reply_callback) {
  send({ "ECHO", msg }, reply_callback);
  return *this;
}

redis_client&
redis_client::eval(const std::string& script, int numkeys, const std::vector<std::string>& keys, const std::vector<std::string>& args, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "EVAL", script, std::to_string(numkeys) };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  cmd.insert(cmd.end(), args.begin(), args.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::evalsha(const std::string& sha1, int numkeys, const std::vector<std::string>& keys, const std::vector<std::string>& args, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "EVAL", sha1, std::to_string(numkeys) };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  cmd.insert(cmd.end(), args.begin(), args.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::exec(const reply_callback_t& reply_callback) {
  send({ "EXEC" }, reply_callback);
  return *this;
}

redis_client&
redis_client::exists(const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "EXISTS" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::expire(const std::string& key, int seconds, const reply_callback_t& reply_callback) {
  send({ "EXPIRE", key, std::to_string(seconds) }, reply_callback);
  return *this;
}

redis_client&
redis_client::expireat(const std::string& key, int timestamp, const reply_callback_t& reply_callback) {
  send({ "EXPIREAT", key, std::to_string(timestamp) }, reply_callback);
  return *this;
}

redis_client&
redis_client::flushall(const reply_callback_t& reply_callback) {
  send({ "FLUSHALL" }, reply_callback);
  return *this;
}

redis_client&
redis_client::flushdb(const reply_callback_t& reply_callback) {
  send({ "FLUSHDB" }, reply_callback);
  return *this;
}

redis_client&
redis_client::geoadd(const std::string& key, const std::vector<std::tuple<std::string, std::string, std::string>>& long_lat_memb, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "GEOADD", key };
  for (const auto& obj : long_lat_memb) {
    cmd.push_back(std::get<0>(obj));
    cmd.push_back(std::get<1>(obj));
    cmd.push_back(std::get<2>(obj));
  }
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::geohash(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "GEOHASH", key };
  cmd.insert(cmd.end(), members.begin(), members.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::geopos(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "GEOPOS", key };
  cmd.insert(cmd.end(), members.begin(), members.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::geodist(const std::string& key, const std::string& member_1, const std::string& member_2, const std::string& unit, const reply_callback_t& reply_callback) {
  send({ "GEODIST", key, member_1, member_2, unit }, reply_callback);
  return *this;
}

redis_client&
redis_client::get(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "GET", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::getbit(const std::string& key, int offset, const reply_callback_t& reply_callback) {
  send({ "GETBIT", key, std::to_string(offset) }, reply_callback);
  return *this;
}

redis_client&
redis_client::getrange(const std::string& key, int start, int end, const reply_callback_t& reply_callback) {
  send({ "GETRANGE", key, std::to_string(start), std::to_string(end) }, reply_callback);
  return *this;
}

redis_client&
redis_client::getset(const std::string& key, const std::string& val, const reply_callback_t& reply_callback) {
  send({ "GETSET", key, val }, reply_callback);
  return *this;
}

redis_client&
redis_client::hdel(const std::string& key, const std::vector<std::string>& fields, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "HDEL", key };
  cmd.insert(cmd.end(), fields.begin(), fields.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::hexists(const std::string& key, const std::string& field, const reply_callback_t& reply_callback) {
  send({ "HEXISTS", key, field }, reply_callback);
  return *this;
}

redis_client&
redis_client::hget(const std::string& key, const std::string& field, const reply_callback_t& reply_callback) {
  send({ "HGET", key, field }, reply_callback);
  return *this;
}

redis_client&
redis_client::hgetall(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "HGETALL", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::hincrby(const std::string& key, const std::string& field, int incr, const reply_callback_t& reply_callback) {
  send({ "HINCRBY", key, field, std::to_string(incr) }, reply_callback);
  return *this;
}

redis_client&
redis_client::hincrbyfloat(const std::string& key, const std::string& field, float incr, const reply_callback_t& reply_callback) {
  send({ "HINCRBYFLOAT", key, field, std::to_string(incr) }, reply_callback);
  return *this;
}

redis_client&
redis_client::hkeys(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "HKEYS", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::hlen(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "HLEN", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::hmget(const std::string& key, const std::vector<std::string>& fields, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "HMGET", key };
  cmd.insert(cmd.end(), fields.begin(), fields.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::hmset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& field_val, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "HMSET", key };
  for (const auto& obj : field_val) {
    cmd.push_back(obj.first);
    cmd.push_back(obj.second);
  }
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::hset(const std::string& key, const std::string& field, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "HSET", key, field, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::hsetnx(const std::string& key, const std::string& field, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "HSETNX", key, field, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::hstrlen(const std::string& key, const std::string& field, const reply_callback_t& reply_callback) {
  send({ "HSTRLEN", key, field }, reply_callback);
  return *this;
}

redis_client&
redis_client::hvals(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "HVALS", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::incr(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "INCR", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::incrby(const std::string& key, int incr, const reply_callback_t& reply_callback) {
  send({ "INCRBY", key, std::to_string(incr) }, reply_callback);
  return *this;
}

redis_client&
redis_client::incrbyfloat(const std::string& key, float incr, const reply_callback_t& reply_callback) {
  send({ "INCRBYFLOAT", key, std::to_string(incr) }, reply_callback);
  return *this;
}

redis_client&
redis_client::info(const std::string& section, const reply_callback_t& reply_callback) {
  send({ "INFO", section }, reply_callback);
  return *this;
}

redis_client&
redis_client::keys(const std::string& pattern, const reply_callback_t& reply_callback) {
  send({ "KEYS", pattern }, reply_callback);
  return *this;
}

redis_client&
redis_client::lastsave(const reply_callback_t& reply_callback) {
  send({ "LASTSAVE" }, reply_callback);
  return *this;
}

redis_client&
redis_client::lindex(const std::string& key, int index, const reply_callback_t& reply_callback) {
  send({ "LINDEX", key, std::to_string(index) }, reply_callback);
  return *this;
}

redis_client&
redis_client::linsert(const std::string& key, const std::string& before_after, const std::string& pivot, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "LINSERT", key, before_after, pivot, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::llen(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "LLEN", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::lpop(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "LPOP", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::lpush(const std::string& key, const std::vector<std::string>& values, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "LPUSH", key };
  cmd.insert(cmd.end(), values.begin(), values.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::lpushx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "LPUSHX", key, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::lrange(const std::string& key, int start, int stop, const reply_callback_t& reply_callback) {
  send({ "LRANGE", key, std::to_string(start), std::to_string(stop) }, reply_callback);
  return *this;
}

redis_client&
redis_client::lrem(const std::string& key, int count, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "LREM", key, std::to_string(count), value }, reply_callback);
  return *this;
}

redis_client&
redis_client::lset(const std::string& key, int index, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "LSET", key, std::to_string(index), value }, reply_callback);
  return *this;
}

redis_client&
redis_client::ltrim(const std::string& key, int start, int stop, const reply_callback_t& reply_callback) {
  send({ "LTRIM", key, std::to_string(start), std::to_string(stop) }, reply_callback);
  return *this;
}

redis_client&
redis_client::mget(const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "MGET" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::migrate(const std::string& host, int port, const std::string& key, const std::string& dest_db, int timeout, bool copy, bool replace, const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "MIGRATE", host, std::to_string(port), key, dest_db, std::to_string(timeout) };
  if (copy) { cmd.push_back("COPY"); }
  if (replace) { cmd.push_back("REPLACE"); }
  if (keys.size()) {
    cmd.push_back("KEYS");
    cmd.insert(cmd.end(), keys.begin(), keys.end());
  }
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::monitor(const reply_callback_t& reply_callback) {
  send({ "MONITOR" }, reply_callback);
  return *this;
}

redis_client&
redis_client::move(const std::string& key, const std::string& db, const reply_callback_t& reply_callback) {
  send({ "MOVE", key, db }, reply_callback);
  return *this;
}

redis_client&
redis_client::mset(const std::vector<std::pair<std::string, std::string>>& key_vals, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "MSET" };
  for (const auto& obj : key_vals) {
    cmd.push_back(obj.first);
    cmd.push_back(obj.second);
  }
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::msetnx(const std::vector<std::pair<std::string, std::string>>& key_vals, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "MSETNX" };
  for (const auto& obj : key_vals) {
    cmd.push_back(obj.first);
    cmd.push_back(obj.second);
  }
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::multi(const reply_callback_t& reply_callback) {
  send({ "MULTI" }, reply_callback);
  return *this;
}

redis_client&
redis_client::object(const std::string& subcommand, const std::vector<std::string>& args, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "OBJECT", subcommand };
  cmd.insert(cmd.end(), args.begin(), args.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::persist(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "PERSIST", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::pexpire(const std::string& key, int milliseconds, const reply_callback_t& reply_callback) {
  send({ "PEXPIRE", key, std::to_string(milliseconds) }, reply_callback);
  return *this;
}

redis_client&
redis_client::pexpireat(const std::string& key, int milliseconds_timestamp, const reply_callback_t& reply_callback) {
  send({ "PEXPIREAT", key, std::to_string(milliseconds_timestamp) }, reply_callback);
  return *this;
}

redis_client&
redis_client::pfadd(const std::string& key, const std::vector<std::string>& elements, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "PFADD", key };
  cmd.insert(cmd.end(), elements.begin(), elements.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::pfcount(const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "PFCOUNT" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::pfmerge(const std::string& destkey, const std::vector<std::string>& sourcekeys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "PFMERGE", destkey };
  cmd.insert(cmd.end(), sourcekeys.begin(), sourcekeys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::ping(const reply_callback_t& reply_callback) {
  send({ "PING" }, reply_callback);
  return *this;
}

redis_client&
redis_client::ping(const std::string& message, const reply_callback_t& reply_callback) {
  send({ "PING", message }, reply_callback);
  return *this;
}

redis_client&
redis_client::psetex(const std::string& key, int milliseconds, const std::string& val, const reply_callback_t& reply_callback) {
  send({ "PSETEX", key, std::to_string(milliseconds), val }, reply_callback);
  return *this;
}

redis_client&
redis_client::pubsub(const std::string& subcommand, const std::vector<std::string>& args, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "PUBSUB", subcommand };
  cmd.insert(cmd.end(), args.begin(), args.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::pttl(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "PTTL", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::quit(const reply_callback_t& reply_callback) {
  send({ "QUIT" }, reply_callback);
  return *this;
}

redis_client&
redis_client::randomkey(const reply_callback_t& reply_callback) {
  send({ "RANDOMKEY" }, reply_callback);
  return *this;
}

redis_client&
redis_client::readonly(const reply_callback_t& reply_callback) {
  send({ "READONLY" }, reply_callback);
  return *this;
}

redis_client&
redis_client::readwrite(const reply_callback_t& reply_callback) {
  send({ "READWRITE" }, reply_callback);
  return *this;
}

redis_client&
redis_client::rename(const std::string& key, const std::string& newkey, const reply_callback_t& reply_callback) {
  send({ "RENAME", key, newkey }, reply_callback);
  return *this;
}

redis_client&
redis_client::renamenx(const std::string& key, const std::string& newkey, const reply_callback_t& reply_callback) {
  send({ "RENAMENX", key, newkey }, reply_callback);
  return *this;
}

redis_client&
redis_client::restore(const std::string& key, int ttl, const std::string& serialized_value, const reply_callback_t& reply_callback) {
  send({ "RESTORE", key, std::to_string(ttl), serialized_value }, reply_callback);
  return *this;
}

redis_client&
redis_client::restore(const std::string& key, int ttl, const std::string& serialized_value, const std::string& replace, const reply_callback_t& reply_callback) {
  send({ "RESTORE", key, std::to_string(ttl), serialized_value, replace }, reply_callback);
  return *this;
}

redis_client&
redis_client::role(const reply_callback_t& reply_callback) {
  send({ "ROLE" }, reply_callback);
  return *this;
}

redis_client&
redis_client::rpop(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "RPOP", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::rpoplpush(const std::string& source, const std::string& destination, const reply_callback_t& reply_callback) {
  send({ "RPOPLPUSH", source, destination }, reply_callback);
  return *this;
}

redis_client&
redis_client::rpush(const std::string& key, const std::vector<std::string>& values, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "RPUSH", key };
  cmd.insert(cmd.end(), values.begin(), values.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::rpushx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "RPUSHX", key, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::sadd(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SADD", key };
  cmd.insert(cmd.end(), members.begin(), members.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::save(const reply_callback_t& reply_callback) {
  send({ "SAVE" }, reply_callback);
  return *this;
}

redis_client&
redis_client::scard(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "SCARD", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::script_debug(const std::string& mode, const reply_callback_t& reply_callback) {
  send({ "SCRIPT", "DEBUG", mode }, reply_callback);
  return *this;
}

redis_client&
redis_client::script_exists(const std::vector<std::string>& scripts, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SCRIPT", "EXISTS" };
  cmd.insert(cmd.end(), scripts.begin(), scripts.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::script_flush(const reply_callback_t& reply_callback) {
  send({ "SCRIPT", "FLUSH" }, reply_callback);
  return *this;
}

redis_client&
redis_client::script_kill(const reply_callback_t& reply_callback) {
  send({ "SCRIPT", "KILL" }, reply_callback);
  return *this;
}

redis_client&
redis_client::script_load(const std::string& script, const reply_callback_t& reply_callback) {
  send({ "SCRIPT", "LOAD", script }, reply_callback);
  return *this;
}

redis_client&
redis_client::sdiff(const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SDIFF" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::sdiffstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SDIFFSTORE", destination };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::select(int index, const reply_callback_t& reply_callback) {
  send({ "SELECT", std::to_string(index) }, reply_callback);
  return *this;
}

redis_client&
redis_client::set(const std::string& key, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "SET", key, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::set_advanced(const std::string& key, const std::string& value, bool ex, int ex_sec, bool px, int px_milli, bool nx, bool xx, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SET", key, value };
  if (ex) {
    cmd.push_back("EX");
    cmd.push_back(std::to_string(ex_sec));
  }
  if (px) {
    cmd.push_back("PX");
    cmd.push_back(std::to_string(px_milli));
  }
  if (nx) { cmd.push_back("NX"); }
  if (xx) { cmd.push_back("XX"); }
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::setbit(const std::string& key, int offset, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "SETBIT", key, std::to_string(offset), value }, reply_callback);
  return *this;
}

redis_client&
redis_client::setex(const std::string& key, int seconds, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "SETEX", key, std::to_string(seconds), value }, reply_callback);
  return *this;
}

redis_client&
redis_client::setnx(const std::string& key, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "SETNX", key, value }, reply_callback);
  return *this;
}

redis_client&
redis_client::setrange(const std::string& key, int offset, const std::string& value, const reply_callback_t& reply_callback) {
  send({ "SETRANGE", key, std::to_string(offset), value }, reply_callback);
  return *this;
}

redis_client&
redis_client::shutdown(const reply_callback_t& reply_callback) {
  send({ "SHUTDOWN" }, reply_callback);
  return *this;
}

redis_client&
redis_client::shutdown(const std::string& save, const reply_callback_t& reply_callback) {
  send({ "SHUTDOWN", save }, reply_callback);
  return *this;
}

redis_client&
redis_client::sinter(const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SINTER" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::sinterstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SINTERSTORE", destination };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::sismember(const std::string& key, const std::string& member, const reply_callback_t& reply_callback) {
  send({ "SISMEMBER", key, member }, reply_callback);
  return *this;
}

redis_client&
redis_client::slaveof(const std::string& host, int port, const reply_callback_t& reply_callback) {
  send({ "SLAVEOF", host, std::to_string(port) }, reply_callback);
  return *this;
}

redis_client&
redis_client::slowlog(const std::string subcommand, const reply_callback_t& reply_callback) {
  send({ "SLOWLOG", subcommand }, reply_callback);
  return *this;
}

redis_client&
redis_client::slowlog(const std::string subcommand, const std::string& argument, const reply_callback_t& reply_callback) {
  send({ "SLOWLOG", subcommand, argument }, reply_callback);
  return *this;
}

redis_client&
redis_client::smembers(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "SMEMBERS", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::smove(const std::string& source, const std::string& destination, const std::string& member, const reply_callback_t& reply_callback) {
  send({ "SMOVE", source, destination, member }, reply_callback);
  return *this;
}

redis_client&
redis_client::spop(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "SPOP", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::spop(const std::string& key, int count, const reply_callback_t& reply_callback) {
  send({ "SPOP", key, std::to_string(count) }, reply_callback);
  return *this;
}

redis_client&
redis_client::srandmember(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "SRANDMEMBER", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::srandmember(const std::string& key, int count, const reply_callback_t& reply_callback) {
  send({ "SRANDMEMBER", key, std::to_string(count) }, reply_callback);
  return *this;
}

redis_client&
redis_client::srem(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SREM", key };
  cmd.insert(cmd.end(), members.begin(), members.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::strlen(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "STRLEN", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::sunion(const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SUNION" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::sunionstore(const std::string& destination, const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "SUNIONSTORE", destination };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::sync(const reply_callback_t& reply_callback) {
  send({ "SYNC" }, reply_callback);
  return *this;
}

redis_client&
redis_client::time(const reply_callback_t& reply_callback) {
  send({ "TIME" }, reply_callback);
  return *this;
}

redis_client&
redis_client::ttl(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "TTL", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::type(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "TYPE", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::unwatch(const reply_callback_t& reply_callback) {
  send({ "UNWATCH" }, reply_callback);
  return *this;
}

redis_client&
redis_client::wait(int numslaves, int timeout, const reply_callback_t& reply_callback) {
  send({ "WAIT", std::to_string(numslaves), std::to_string(timeout) }, reply_callback);
  return *this;
}

redis_client&
redis_client::watch(const std::vector<std::string>& keys, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "WATCH" };
  cmd.insert(cmd.end(), keys.begin(), keys.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::zcard(const std::string& key, const reply_callback_t& reply_callback) {
  send({ "ZCARD", key }, reply_callback);
  return *this;
}

redis_client&
redis_client::zcount(const std::string& key, int min, int max, const reply_callback_t& reply_callback) {
  send({ "ZCOUNT", key, std::to_string(min), std::to_string(max) }, reply_callback);
  return *this;
}

redis_client&
redis_client::zincrby(const std::string& key, int incr, const std::string& member, const reply_callback_t& reply_callback) {
  send({ "ZINCRBY", key, std::to_string(incr), member }, reply_callback);
  return *this;
}

redis_client&
redis_client::zlexcount(const std::string& key, int min, int max, const reply_callback_t& reply_callback) {
  send({ "ZLEXCOUNT", key, std::to_string(min), std::to_string(max) }, reply_callback);
  return *this;
}

redis_client&
redis_client::zrange(const std::string& key, int start, int stop, bool withscores, const reply_callback_t& reply_callback) {
  if (withscores)
    send({ "ZRANGE", key, std::to_string(start), std::to_string(stop), "WITHSCORES" }, reply_callback);
  else
    send({ "ZRANGE", key, std::to_string(start), std::to_string(stop) }, reply_callback);
  return *this;
}

redis_client&
redis_client::zrank(const std::string& key, const std::string& member, const reply_callback_t& reply_callback) {
  send({ "ZRANK", key, member }, reply_callback);
  return *this;
}

redis_client&
redis_client::zrem(const std::string& key, const std::vector<std::string>& members, const reply_callback_t& reply_callback) {
  std::vector<std::string> cmd = { "ZREM", key };
  cmd.insert(cmd.end(), members.begin(), members.end());
  send(cmd, reply_callback);
  return *this;
}

redis_client&
redis_client::zremrangebylex(const std::string& key, int min, int max, const reply_callback_t& reply_callback) {
  send({ "ZREMRANGEBYLEX", key, std::to_string(min), std::to_string(max) }, reply_callback);
  return *this;
}

redis_client&
redis_client::zremrangebyrank(const std::string& key, int start, int stop, const reply_callback_t& reply_callback) {
  send({ "ZREMRANGEBYRANK", key, std::to_string(start), std::to_string(stop) }, reply_callback);
  return *this;
}

redis_client&
redis_client::zremrangebyscore(const std::string& key, int min, int max, const reply_callback_t& reply_callback) {
  send({ "ZREMRANGEBYSCORE", key, std::to_string(min), std::to_string(max) }, reply_callback);
  return *this;
}

redis_client&
redis_client::zrevrange(const std::string& key, int start, int stop, bool withscores, const reply_callback_t& reply_callback) {
  if (withscores)
    send({ "ZREVRANGE", key, std::to_string(start), std::to_string(stop), "WITHSCORES" }, reply_callback);
  else
    send({ "ZREVRANGE", key, std::to_string(start), std::to_string(stop) }, reply_callback);
  return *this;
}

redis_client&
redis_client::zrevrank(const std::string& key, const std::string& member, const reply_callback_t& reply_callback) {
  send({ "ZREVRANK", key, member }, reply_callback);
  return *this;
}

redis_client&
redis_client::zscore(const std::string& key, const std::string& member, const reply_callback_t& reply_callback) {
  send({ "ZSCORE", key, member }, reply_callback);
  return *this;
}

} //! cpp_redis
