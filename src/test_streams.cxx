#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <zmq.hpp>

// Optional - for formatting text and getting input
#include <fmt/core.h>
#include "clipp.h"

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

void server(zmq::context_t& context, std::string connection_string, int port = 0) {
  if (port > 0) {
    connection_string = fmt::format("tcp://*:{}", port);
  }
  zmq::socket_t socket{context, zmq::socket_type::rep};
  socket.bind(connection_string);
  // fmt::print("Starting server on with {}\n", connection_string);

  auto msg_data = std::make_unique<zmq::message_t>();

  for (;;) {
    auto out = socket.recv(*msg_data, zmq::recv_flags::none);
    // Check if we should stop the server by sending a zero vector
    auto x = msg_data.get()->size();
    if (x == 0) return;
    // Do something with the data here
    socket.send(*msg_data, zmq::send_flags::none);
  }
}

void kill_server(std::string connection_string) {
  zmq::context_t context{1};
  // construct a REQ (request) socket and connect to interface
  zmq::socket_t socket{context, zmq::socket_type::req};
  socket.connect(connection_string);
  std::vector<float> zero = {};
  auto zero_data = std::make_unique<zmq::message_t>(zero);
  socket.send(*zero_data, zmq::send_flags::none);
}

void client(zmq::context_t& context, std::string connection_string, int length, int num, bool __kill = false) {
  // construct a REQ (request) socket and connect to interface
  zmq::socket_t socket{context, zmq::socket_type::req};
  socket.connect(connection_string);

  auto vec_data = gen_random_vec<float>(length);
  std::vector<float> times = {};

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
  }

  if (__kill) kill_server(connection_string);

  auto sum = std::accumulate(times.begin(), times.end(), 0.0);
  auto avg = sum / float(times.size());

  double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
  double stdev = std::sqrt(sq_sum / times.size() - avg * avg);
  auto vec_size = vector_size(vec_data);

  fmt::println(
      "{{\"socket\": \"{}\", \"number\": {}, \"length\": {}, \"size_bytes\":{} ,\"avgtime\": {}, \"stdev\": {}}}",
      connection_string, num, length, vec_size, avg, stdev);
}

int main(int argc, char** argv) {
  int num = 1000;
  int length = 1000;

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
       clipp::option("-o", "--oneshot").set(one_shot, true).doc("Run client once and kill server"),
       clipp::option("-k", "--kill").set(_kill_server, true).doc("Kill the server"),
       clipp::option("-n", "--num") & clipp::value("num", num).doc("Number of messages to pass between processes"),
       clipp::option("-l", "--length") &
           clipp::value("length", length).doc("Length of a single message vector to pass"));

  if (!clipp::parse(argc, argv, cli) || (use_inproc && (run_server || run_client))) {
    std::cout << clipp::make_man_page(cli, argv[0]);
    exit(2);
  }

  std::string connection_string;

  if (use_ipc) {
    connection_string = ipc_string;
    // fmt::println("Using ipc {}", connection_string);
  } else if (use_inproc) {
    connection_string = inproc_string;
    // fmt::println("Using inproc {}", connection_string);
  } else {
    port = (port == 0) ? 5555 : port;
    connection_string = fmt::format("tcp://{}:{}", host, port);
    // fmt::println("Using TCP {}", connection_string);
  }

  // initialize the zmq context with a single IO thread
  zmq::context_t context{static_cast<int>(std::thread::hardware_concurrency())};
  if (_kill_server) {
    fmt::println("Killing server at {}", connection_string);
    kill_server(connection_string);
  } else if (run_server) {
    std::thread server_thread(server, std::ref(context), connection_string, port);
    server_thread.join();
  } else if (run_client) {
    std::thread client_thread(client, std::ref(context), connection_string, length, num, one_shot);
    client_thread.join();
  } else {
    std::thread server_thread(server, std::ref(context), connection_string, port);
    std::thread client_thread(client, std::ref(context), connection_string, length, num, one_shot);
    client_thread.join();
    server_thread.join();
  }

  /* code */
  return 0;
}
