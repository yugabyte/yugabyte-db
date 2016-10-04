#pragma once

#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

namespace cpp_redis {

namespace network {

class io_service {
public:
  //! instance getter (singleton pattern)
  static io_service& get_instance(void);

private:
  //! ctor & dtor
  io_service(void);
  ~io_service(void);

  //! copy ctor & assignment operator
  io_service(const io_service&) = delete;
  io_service& operator=(const io_service&) = delete;

public:
  //! disconnection handler declaration
  typedef std::function<void(io_service&)> disconnection_handler_t;

  //! add or remove a given fd from the io service
  //! untrack should never be called from inside a callback
  void track(int fd, const disconnection_handler_t& handler);
  void untrack(int fd);

  //! asynchronously read read_size bytes and append them to the given buffer
  //! on completion, call the read_callback to notify of the success or failure of the operation
  //! return false if another async_read operation is in progress or fd is not registered
  typedef std::function<void(std::size_t)> read_callback_t;
  bool async_read(int fd, std::vector<char>& buffer, std::size_t read_size, const read_callback_t& callback);

  //! asynchronously write write_size bytes from buffer to the specified fd
  //!on completion, call the write_callback to notify of the success or failure of the operation
  //! return false if another async_write operation is in progress or fd is not registered
  typedef std::function<void(std::size_t)> write_callback_t;
  bool async_write(int fd, const std::vector<char>& buffer, std::size_t write_size, const write_callback_t& callback);

private:
  //! simple struct to keep track of ongoing operations on a given fd
  struct fd_info {
    disconnection_handler_t disconnection_handler;

    std::atomic_bool async_read;
    std::vector<char>* read_buffer;
    std::size_t read_size;
    read_callback_t read_callback;

    std::atomic_bool async_write;
    std::vector<char> write_buffer;
    std::size_t write_size;
    write_callback_t write_callback;
  };

private:
  //! listen for incoming events and notify
  void listen(void);

  //! notify the select call so that it can wake up to process new events
  void notify_select(void);

private:
  //! select fds sets handling (init, rd/wr handling)
  int init_sets(fd_set* rd_set, fd_set* wr_set);
  void process_sets(fd_set* rd_set, fd_set* wr_set);

  typedef std::function<void()> callback_t;
  callback_t read_fd(int fd);
  callback_t write_fd(int fd);

private:
  //! whether the worker should terminate or not
  std::atomic_bool m_should_stop;

  //! worker in the background, listening for events
  std::thread m_worker;

  //! tracked fds
  std::unordered_map<int, fd_info> m_fds;

  //! fd associated to the pipe used to wake up the select call
  int m_notif_pipe_fds[2];

  //! mutex to protect m_fds access against race condition
  //!
  //! specific mutex for untrack: we dont want someone to untrack a fd while we process it
  //! this behavior could cause some issues when executing callbacks in another thread
  //! for example, obj is destroyed, in its dtor it untracks the fd, but at the same time
  //! a callback is executed from within another thread: the untrack mutex avoid this without being costly
  std::recursive_mutex m_fds_mutex;
};

} //! network

} //! cpp_redis
