#include "tun.hpp"

#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <glog/logging.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <boost/program_options.hpp>
#include <atomic>
#include <iomanip>
#include <utility>

using boost::asio::ip::udp;
using namespace std;

constexpr size_t max_length = 65536;
enum class Type { Server, Client };

template <typename T>
class Counter {
public:
  Counter(std::string name) :name(move(name)) { }

  void Increase(T delta) {
    counter += delta;
  }
  std::string PrintSinceLastUpdate() {
    T value = counter;
    auto now = chrono::high_resolution_clock::now();
    std::chrono::duration<double> d = now - last_update;
    auto d1 = d.count() == 0 ? 1 : d.count();
    auto rate = (value - last_value) / d1;
    last_value = value;
    last_update = now;

    std::stringstream ss;
    ss << name << ": " << std::setw(2) << rate << " B/s";
    return ss.str();
  }
private:
  std::atomic<T> counter{};
  T last_value{};
  std::chrono::high_resolution_clock::time_point last_update{};
  std::string name;
};

void run(boost::asio::io_context& io_context,
    const std::string &host,
    unsigned short port,
    Type type,
    int tunfd) {

  udp::socket local_sock(io_context, udp::endpoint(
      boost::asio::ip::make_address_v4(type == Type::Server ? host : "0.0.0.0"), type == Type::Server ? port : 0));

  udp::endpoint peer_endpoint(
      boost::asio::ip::make_address_v4(type == Type::Server ? "0.0.0.0" : host), type == Type::Server ? 0 : port);

  std::atomic<bool> connected = false;
  Counter<int64_t> counters[] = {{"sent_tun"}, {"received_tun"}, {"sent_sock"}, {"received_sock"}};

  thread sock2tun([&local_sock, type, tunfd, &peer_endpoint, &connected, &counters]() {
    udp::endpoint sender_endpoint;
    for (;;) {
      char data[max_length];
      size_t length = local_sock.receive_from(
          boost::asio::buffer(data, max_length), sender_endpoint);

      counters[3].Increase(length);

      if (type == Type::Server) {
        peer_endpoint = sender_endpoint;
        connected = true;
      } else {
        if (peer_endpoint.size() && sender_endpoint != peer_endpoint)
          continue; // just ignore
      }

      auto nwrite = write(tunfd, data, length);
      CHECK(nwrite == length);
      counters[0].Increase(nwrite);
    }
  });

  thread tun2sock([&local_sock, type, tunfd, &peer_endpoint, &connected, &counters]() {
    for (;;) {
      char data[max_length];
      auto nread = read(tunfd, data, max_length);
      CHECK(nread > 0);
      counters[1].Increase(nread);

      if (type == Type::Server && !connected) {
        continue;
      }
      auto nwrite = local_sock.send_to(boost::asio::buffer(data, nread), peer_endpoint);
      CHECK(nwrite == nread);
      counters[2].Increase(nread);
    }
  });

  thread([&]() {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      stringstream ss;
      for (auto &counter : counters) {
        ss << counter.PrintSinceLastUpdate() << ", ";
      }
      LOG(WARNING) << ss.str();
    }
  }).detach();

  sock2tun.join();
  tun2sock.join();
}

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
  try {
    std::string tun_if_name;
    std::string host;
    unsigned short port;
    bool is_server{};

    po::options_description desc_env;
    desc_env.add_options()
        ("tun-if-name", po::value(&tun_if_name)->default_value("tun1"))

        ("host", po::value(&host)->default_value(""))
        ("port", po::value(&port)->default_value(1234))
        ("server,s", po::value(&is_server))
        ;

    po::variables_map vm_env;

    boost::program_options::store(
        boost::program_options::parse_command_line(argc, argv, desc_env),
        vm_env
    );
    boost::program_options::notify(vm_env);

    Type type = is_server ? Type::Server : Type::Client;

    boost::asio::io_context io_context;
    char tundevname[32] = {0};
    strcpy(tundevname, tun_if_name.c_str());
    int tunfd = tun_alloc(tundevname, IFF_TUN | IFF_NO_PI);
    if (tunfd < 0) {
      perror("tunalloc");
      abort();
    }
    run(io_context, host, port, type, tunfd);
  }
  catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
