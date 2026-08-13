#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <zmq.hpp>

// Optional - for formatting text and getting input
#include <fmt/color.h>
#include <fmt/core.h>
#include "clipp.h"

#define __size__ double

struct ServerStats {
  std::atomic<bool> active{false};
  std::atomic<size_t> bytes{0};
};

int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

template <typename T>
std::vector<T> gen_random_vec(size_t length = 10000) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<T> dis(0.0, 128.0);
  std::vector<T> random_floats(length);
  std::generate(random_floats.begin(), random_floats.end(), [&]() { return dis(gen); });

  return random_floats;
}

template <typename T>
size_t vector_size(const typename std::vector<T>& vec) {
  return sizeof(T) * vec.size();
}

std::vector<long> parse_list(const std::string& s) {
  std::vector<long> out;
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    if (!token.empty()) out.push_back(std::stol(token));
  }
  return out;
}

void printer(ServerStats& stats) {
  char spinner[] = {'|', '/', '-', '\\'};
  int i = 0;
  std::deque<std::pair<int64_t, size_t>> samples;
  for (;;) {
    const int64_t now = now_ns();
    const size_t bytes = stats.bytes.load(std::memory_order_relaxed);
    samples.push_back({now, bytes});
    while (samples.size() > 1 && now - samples.front().first > 1000000000LL) {
      samples.pop_front();
    }
    double mbps = 0.0;
    if (samples.size() > 1) {
      const auto& oldest = samples.front();
      double elapsed = (now - oldest.first) / 1e9;
      mbps = elapsed > 0.0 ? (bytes - oldest.second) / elapsed / 1e6 : 0.0;
    }
    if (stats.active.load(std::memory_order_relaxed)) {
      fmt::print(fg(fmt::color::green_yellow), " Someone connected! {} MB/s: {:.2f}      \r", spinner[i++ % 4], mbps);
    } else {
      fmt::print(fg(fmt::color::blue_violet), " Server Running {}\r", spinner[i++ % 4]);
    }
    std::cout.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void server(zmq::context_t& context, std::string connection_string, int port, ServerStats& stats) {
  if (port > 0) {
    connection_string = fmt::format("tcp://*:{}", port);
  }
  zmq::socket_t socket{context, zmq::socket_type::rep};
  socket.bind(connection_string);
  // fmt::print("Starting server on with {}\n", connection_string);

  auto msg_data = std::make_unique<zmq::message_t>();

  for (;;) {
    auto out = socket.recv(*msg_data, zmq::recv_flags::none);
    stats.active.store(true, std::memory_order_relaxed);
    // Check if we should stop the server by sending a zero vector
    auto x = msg_data.get()->size();
    if (x == 0) return;
    stats.bytes.fetch_add(x, std::memory_order_relaxed);
    // Do something with the data here
    socket.send(*msg_data, zmq::send_flags::none);
  }
}

void kill_server(std::string connection_string) {
  zmq::context_t context{1};
  // construct a REQ (request) socket and connect to interface
  zmq::socket_t socket{context, zmq::socket_type::req};
  socket.connect(connection_string);
  std::vector<__size__> zero = {};
  auto zero_data = std::make_unique<zmq::message_t>(zero);
  socket.send(*zero_data, zmq::send_flags::none);
}

void client(zmq::context_t& context, std::string connection_string, int length, int num) {
  // construct a REQ (request) socket and connect to interface
  zmq::socket_t socket{context, zmq::socket_type::req};
  socket.connect(connection_string);

  auto vec_data = gen_random_vec<__size__>(length);
  auto vec_size = vector_size(vec_data);
  std::vector<float> times = {};
  times.reserve(num);

  const auto t0 = std::chrono::steady_clock::now();
  const int progress_step = std::max(100, num / 100);
  bool progress_printed = false;

  for (auto request_num = 0; request_num < num; ++request_num) {
    const auto p1 = std::chrono::high_resolution_clock::now();
    auto start = std::chrono::duration_cast<std::chrono::nanoseconds>(p1.time_since_epoch()).count();

    auto msg_data = std::make_unique<zmq::message_t>(vec_data);
    socket.send(*msg_data, zmq::send_flags::none);

    auto reply = std::make_unique<zmq::message_t>();
    auto out = socket.recv(*reply, zmq::recv_flags::none);

    const auto p2 = std::chrono::high_resolution_clock::now();
    auto end = std::chrono::duration_cast<std::chrono::nanoseconds>(p2.time_since_epoch()).count();
    std::chrono::duration<double, std::nano> nano_duration(end - start);
    auto seconds_duration = std::chrono::duration_cast<std::chrono::duration<double>>(nano_duration);
    times.push_back(seconds_duration.count());

    if (request_num > 0 && request_num % progress_step == 0) {
      auto now = std::chrono::steady_clock::now();
      double elapsed = std::chrono::duration<double>(now - t0).count();
      double est_mbps = elapsed > 0.0 ? (request_num * vec_size) / (elapsed * 1e6) : 0.0;
      fmt::print(stderr, "    {}/{} msgs, est {:.2f} MB/s\r", request_num, num, est_mbps);
      progress_printed = true;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  if (progress_printed) fmt::print(stderr, "\n");

  double total_time = std::chrono::duration<double>(t1 - t0).count();
  auto sum = std::accumulate(times.begin(), times.end(), 0.0);
  auto avg = sum / float(times.size());

  double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
  double stdev = std::sqrt(sq_sum / times.size() - avg * avg);

  double mbps = total_time > 0.0 ? (num * vec_size) / (total_time * 1e6) : 0.0;

  fmt::println(
      "{{\"socket\": \"{}\", \"number\": {}, \"length\": {}, \"size_bytes\": {}, \"total_time\": {:.6f}, "
      "\"avgtime\": {:.9f}, \"stdev\": {:.9f}, \"mbps\": {:.3f}}}",
      connection_string, num, length, vec_size, total_time, avg, stdev, mbps);
}

void run_client_tests(zmq::context_t& context, std::string connection_string, const std::vector<long>& lengths,
                      const std::vector<long>& nums, bool kill_at_end) {
  const size_t total = lengths.size() * nums.size();
  size_t done = 0;
  for (auto length : lengths) {
    for (auto num : nums) {
      ++done;
      fmt::print(stderr, "[{}/{}] length={}, num={}\n", done, total, length, num);
      client(context, connection_string, static_cast<int>(length), static_cast<int>(num));
    }
  }
  if (kill_at_end) kill_server(connection_string);
}

int main(int argc, char** argv) {
  std::string num = "1000";
  std::string length = "1000";

  bool run_server = false;
  bool run_client = false;
  bool one_shot = false;
  bool _kill_server = false;

  // For TCP connections
  std::string host = "localhost";
  int port = 0;

  // For ipc
  bool use_ipc = false;
  std::string ipc_string = "ipc:///tmp/zmq_socket";

  // For inproc
  bool use_inproc = false;
  std::string inproc_string = "inproc://inproc_socket";

  auto cli =
      (clipp::option("-x", "--inproc").set(use_inproc, true).doc("Run in inproc mode"),
       clipp::option("-i", "--ipc").set(use_ipc, true).doc("Run in ipc mode"),
       clipp::option("-p", "--port") & clipp::value("port", port).doc("Port for connecting with tcp"),
       clipp::option("-h", "--host") & clipp::value("host", host).doc("Host for connecting with tcp"),
       clipp::option("-s", "--server").set(run_server, true).doc("run in server mode, cannot be used with \"inproc\""),
       clipp::option("-c", "--client").set(run_client, true).doc("run in client mode, cannot be used with \"inproc\""),
       clipp::option("-o", "--oneshot").set(one_shot, true).doc("Run all tests then kill server"),
       clipp::option("-k", "--kill").set(_kill_server, true).doc("Kill the server"),
       clipp::option("-n", "--num") & clipp::value("num", num)
           .doc("Comma-separated list of message counts to run"),
       clipp::option("-l", "--length") &
           clipp::value("length", length).doc("Comma-separated list of message vector lengths"));

  if (!clipp::parse(argc, argv, cli) || (use_inproc && (run_server || run_client))) {
    std::cout << clipp::make_man_page(cli, argv[0]);
    exit(2);
  }

  std::string connection_string;

  if (use_ipc) {
    connection_string = ipc_string;
    fmt::println("Using ipc {}", connection_string);
  } else if (use_inproc) {
    connection_string = inproc_string;
    fmt::println("Using inproc {}", connection_string);
  } else {
    port = (port == 0) ? 5555 : port;
    connection_string = fmt::format("tcp://{}:{}", host, port);
  }

  // initialize the zmq context with a single IO thread
  zmq::context_t context{static_cast<int>(std::thread::hardware_concurrency())};
  std::vector<long> lengths = parse_list(length);
  std::vector<long> nums = parse_list(num);
  if (lengths.empty()) lengths.push_back(1000);
  if (nums.empty()) nums.push_back(1000);

  ServerStats stats;

  if (_kill_server) {
    fmt::println("Killing server at {}", connection_string);
    kill_server(connection_string);
  } else if (run_server) {
    fmt::println("TCP Server at {}", connection_string);
    std::thread server_thread(server, std::ref(context), connection_string, port, std::ref(stats));
    std::thread p(printer, std::ref(stats));
    server_thread.join();
    p.detach();
  } else if (run_client) {
    run_client_tests(context, connection_string, lengths, nums, one_shot);
  } else {
    std::thread server_thread(server, std::ref(context), connection_string, port, std::ref(stats));
    std::thread p(printer, std::ref(stats));
    std::thread client_thread(run_client_tests, std::ref(context), connection_string, lengths, nums, one_shot);
    client_thread.join();
    server_thread.join();
    p.detach();
  }

  /* code */
  return 0;
}
