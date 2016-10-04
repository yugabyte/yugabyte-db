#include "cpp_redis/network/io_service.hpp"
#include "cpp_redis/redis_error.hpp"

#include <fcntl.h>

namespace cpp_redis {

namespace network {

io_service&
io_service::get_instance(void) {
  static io_service instance;
  return instance;
}

io_service::io_service(void)
: m_should_stop(false)
{
  if (pipe(m_notif_pipe_fds) == -1)
    throw cpp_redis::redis_error("Could not init cpp_redis::io_service, pipe() failure");

  int flags = fcntl(m_notif_pipe_fds[1], F_GETFL, 0);
  if (flags == -1 or fcntl(m_notif_pipe_fds[1], F_SETFL, flags | O_NONBLOCK) == -1)
    throw cpp_redis::redis_error("Could not init cpp_redis::io_service, fcntl() failure");

  m_worker = std::thread(&io_service::listen, this);
}

io_service::~io_service(void) {
  m_should_stop = true;
  notify_select();

  m_worker.join();

  close(m_notif_pipe_fds[0]);
  close(m_notif_pipe_fds[1]);
}

int
io_service::init_sets(fd_set* rd_set, fd_set* wr_set) {
  int max_fd = m_notif_pipe_fds[0];

  FD_ZERO(rd_set);
  FD_ZERO(wr_set);
  FD_SET(m_notif_pipe_fds[0], rd_set);

  std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);
  for (const auto& fd : m_fds) {
    if (fd.second.async_read)
      FD_SET(fd.first, rd_set);

    if (fd.second.async_write)
      FD_SET(fd.first, wr_set);

    if ((fd.second.async_read or fd.second.async_write) and fd.first > max_fd)
      max_fd = fd.first;
  }

  return max_fd;
}

io_service::callback_t
io_service::read_fd(int fd) {
  std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);

  auto fd_it = m_fds.find(fd);
  if (fd_it == m_fds.end())
    return nullptr;

  auto& buffer = *fd_it->second.read_buffer;
  int original_buffer_size = buffer.size();
  buffer.resize(original_buffer_size + fd_it->second.read_size);

  int nb_bytes_read = recv(fd_it->first, buffer.data() + original_buffer_size, fd_it->second.read_size, 0);
  fd_it->second.async_read = false;

  if (nb_bytes_read <= 0) {
    buffer.resize(original_buffer_size);
    fd_it->second.disconnection_handler(*this);
    m_fds.erase(fd_it);

    return nullptr;
  }
  else {
    buffer.resize(original_buffer_size + nb_bytes_read);

    return std::bind(fd_it->second.read_callback, nb_bytes_read);
  }
}

io_service::callback_t
io_service::write_fd(int fd) {
  std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);

  auto fd_it = m_fds.find(fd);
  if (fd_it == m_fds.end())
    return nullptr;

  int nb_bytes_written = send(fd_it->first, fd_it->second.write_buffer.data(), fd_it->second.write_size, 0);
  fd_it->second.async_write = false;

  if (nb_bytes_written <= 0) {
    fd_it->second.disconnection_handler(*this);
    m_fds.erase(fd_it);

    return nullptr;
  }
  else
    return std::bind(fd_it->second.write_callback, nb_bytes_written);
}

void
io_service::process_sets(fd_set* rd_set, fd_set* wr_set) {
  std::vector<int> fds_to_read;
  std::vector<int> fds_to_write;

  //! Quickly fetch fds that are readable/writeable
  //! This reduce lock time and avoid possible deadlock in callbacks
  {
    std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);

    for (const auto& fd : m_fds) {
      if (fd.second.async_read and FD_ISSET(fd.first, rd_set))
        fds_to_read.push_back(fd.first);

      if (fd.second.async_write and FD_ISSET(fd.first, wr_set))
        fds_to_write.push_back(fd.first);
    }
  }

  for (int fd : fds_to_read) {
    auto callback = read_fd(fd);
    if (callback)
      callback();
  }
  for (int fd : fds_to_write) {
    auto callback = write_fd(fd);
    if (callback)
      callback();
  }

  if (FD_ISSET(m_notif_pipe_fds[0], rd_set)) {
    char buf[1024];
    read(m_notif_pipe_fds[0], buf, 1024);
  }
}

void
io_service::listen(void) {
  fd_set rd_set;
  fd_set wr_set;

  while (not m_should_stop) {
    int max_fd = init_sets(&rd_set, &wr_set);

    if (select(max_fd + 1, &rd_set, &wr_set, nullptr, nullptr) > 0)
      process_sets(&rd_set, &wr_set);
  }
}

void
io_service::track(int fd, const disconnection_handler_t& handler) {
  std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);

  auto& info = m_fds[fd];
  info.async_read = false;
  info.async_write = false;
  info.disconnection_handler = handler;

  notify_select();
}

void
io_service::untrack(int fd) {
  std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);
  m_fds.erase(fd);
}

bool
io_service::async_read(int fd, std::vector<char>& buffer, std::size_t read_size, const read_callback_t& callback) {
  std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);

  auto reg_fd_it = m_fds.find(fd);
  if (reg_fd_it == m_fds.end())
    return false;

  auto& reg_fd = reg_fd_it->second;
  bool expected = false;
  if (not reg_fd.async_read.compare_exchange_strong(expected, true))
    return false;

  reg_fd.read_buffer = &buffer;
  reg_fd.read_size = read_size;
  reg_fd.read_callback = callback;

  notify_select();

  return true;
}

bool
io_service::async_write(int fd, const std::vector<char>& buffer, std::size_t write_size, const write_callback_t& callback) {
  std::lock_guard<std::recursive_mutex> lock(m_fds_mutex);

  auto reg_fd_it = m_fds.find(fd);
  if (reg_fd_it == m_fds.end())
    return false;

  auto& reg_fd = reg_fd_it->second;
  bool expected = false;
  if (not reg_fd.async_write.compare_exchange_strong(expected, true))
      return false;

  reg_fd.write_buffer = buffer;
  reg_fd.write_size = write_size;
  reg_fd.write_callback = callback;

  notify_select();

  return true;
}

void
io_service::notify_select(void) {
  write(m_notif_pipe_fds[1], "a", 1);
}

} //! network

} //! cpp_redis
