// Copyright (c) YugaByte, Inc.

#include <signal.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "yb/gutil/stringprintf.h"
#include "yb/util/subprocess.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"

constexpr int kWaitSecBeforeSIGKILL = 5;

using std::cerr;
using std::condition_variable;
using std::cout;
using std::endl;
using std::invalid_argument;
using std::mutex;
using std::string;
using std::thread;
using std::unique_lock;
using std::vector;
using std::chrono::steady_clock;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

using yb::Subprocess;
using yb::Status;

int main(int argc, char** argv) {
  yb::InitGoogleLoggingSafeBasic(argv[0]);

  vector<string> args;
  args.assign(argv + 1, argv + argc);
  if (args.size() < 2) {
    cerr << "Usage: run-with-timeout <timeout_sec> <program> [<arguments>]" << endl;
    cerr << endl;
    cerr << "Runs the given program with the given timeout. If the prgoram is still" << endl;
    cerr << "running after the given amount of time, it gets sent the SIGQUIT signal" << endl;
    cerr << "to make it create a core dump and terminate. If it is still running after" << endl;
    cerr << "that, it is killed with a SIGKILL." << endl;
    cerr << endl;
    cerr << "A timeout value of 0 means no timeout." << endl;
    return EXIT_FAILURE;
  }

  const string& timeout_arg = args[0];
  int timeout_sec;
  try {
    timeout_sec = std::stoi(timeout_arg);
  } catch (const invalid_argument& ex) {
    LOG(ERROR) << "Invalid timeout: " << timeout_arg;
    return EXIT_FAILURE;
  }

  if (timeout_sec < 0) {
    LOG(ERROR) << "Invalid timeout: " << timeout_sec;
    return EXIT_FAILURE;
  }
  const string program = args[1];

  // Arguments include the program name, so we only erase the first argument from our own args: the
  // timeout value.
  args.erase(args.begin());
  Subprocess subprocess(program, args);
  auto start_time = steady_clock::now();
  LOG(INFO) << "Starting child process";
  const Status start_status = subprocess.Start();
  if (!start_status.ok()) {
    LOG(ERROR) << "Failed to start child process: " << start_status.ToString();
    return EXIT_FAILURE;
  }
  LOG(INFO) << "Child process pid: " << subprocess.pid();
  mutex lock;
  condition_variable finished_cond;
  bool finished = false;
  bool timed_out = false;

  thread reaper_thread([timeout_sec, &subprocess, &lock, &finished_cond, &finished, &timed_out] {
    if (timeout_sec == 0) {
      return; // no timeout
    }

    // Wait until the main thread notifies us that the process has finished running, or the
    // timeout is reached.
    auto wait_until_when = steady_clock::now() + std::chrono::seconds(timeout_sec);

    bool finished_local = false;
    {
      unique_lock<mutex> l(lock);
      finished_cond.wait_until(l, wait_until_when, [&finished]{ return finished; });
      finished_local = finished;
    }

    if (!finished_local && subprocess.IsRunning()) {
      timed_out = true;
      LOG(ERROR) << "Timeout reached, trying to stop the child process with SIGQUIT";
      const Status sigquit_status = subprocess.KillNoCheckIfRunning(SIGQUIT);
      if (!sigquit_status.ok()) {
        LOG(ERROR) << "Failed to send SIGQUIT to child process: " << sigquit_status.ToString();
      }
      int sleep_ms = 5;
      auto second_wait_until = steady_clock::now() + std::chrono::seconds(kWaitSecBeforeSIGKILL);
      while (subprocess.IsRunning() && steady_clock::now() < second_wait_until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        // Sleep for 5 ms, 10 ms, 15 ms, etc. capped at 100 ms.
        sleep_ms = std::min(sleep_ms + 5, 100);
      }

      if (subprocess.IsRunning()) {
        LOG(ERROR) << "The process is still running after waiting for " << kWaitSecBeforeSIGKILL
                   << " seconds, using SIGKILL";
        const Status sigkill_status = subprocess.KillNoCheckIfRunning(SIGKILL);
        if (!sigkill_status.ok()) {
          LOG(ERROR) << "Failed to send SIGKILL to child process: " << sigkill_status.ToString();
        }
      } else {
        LOG(INFO) << "Child process terminated after we sent SIGQUIT";
      }
    }
  });

  int wait_ret_val = 0;
  subprocess.Wait(&wait_ret_val);
  {
    unique_lock<mutex> l(lock);
    finished = true;
    finished_cond.notify_one();
  }

  reaper_thread.join();
  int exit_code = wait_ret_val >> 8;
  LOG(INFO) << "Child process returned exit code " << exit_code;

  if (exit_code == 0 && timed_out) {
    exit_code = EXIT_FAILURE;
    LOG(ERROR) << "Child process timed out and we had to kill it, returning exit code "
               << exit_code;
  }
  auto duration_ms = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
  LOG(INFO) << "Total time taken: " << StringPrintf("%.3f", duration_ms / 1000.0) << " sec";
  return exit_code;
}
