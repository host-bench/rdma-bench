// MIT License

// Copyright (c) 2022 Duke University. All rights reserved.
// Copyright (c) 2021 ByteDance Inc. All rights reserved.

// See LICENSE for license information

#include <thread>
#include <vector>

#include "rdma_context.hpp"

DEFINE_bool(ctrl, false, "Enable for control test");

int main(int argc, char **argv) {
  ibv_fork_init();
  if (Collie::Initialize(argc, argv)) return -1;

  // Set up server
  LOG(INFO) << "Traffic Engine starts";
  if (FLAGS_server) {
    std::thread listen_thread;
    std::thread server_thread;
    auto pici_server =
        new Collie::rdma_context(FLAGS_dev.c_str(), FLAGS_gid, FLAGS_host_num,
                                 FLAGS_qp_num, FLAGS_print_thp);
    if (pici_server->Init()) {
      LOG(ERROR) << "Collie server initialization failed. Exit...";
      return -1;
    }
    listen_thread = std::thread(&Collie::rdma_context::Listen, pici_server);
    server_thread =
        std::thread(&Collie::rdma_context::ServerDatapath, pici_server);
    LOG(INFO) << "Collie server has started.";
    listen_thread.join();
    server_thread.join();
  }
  // Set up client
  if (FLAGS_connect != "") {
    std::vector<Collie::rdma_context *> clients;
    auto host_vec = Collie::ParseHostlist(FLAGS_connect);
    for (int t = 0; t < FLAGS_thread; t++) {
      auto c = new Collie::rdma_context(FLAGS_dev.c_str(), FLAGS_gid,
                                        host_vec.size(), FLAGS_qp_num,
                                        FLAGS_print_thp);
      if (c->Init()) {
        LOG(ERROR) << "Collie client initialization failed. Exit... ";
        return -1;
      }
      for (size_t i = 0; i < host_vec.size(); i++) {
        if (c->Connect(host_vec[i].c_str(), FLAGS_port, i)) {
          LOG(ERROR) << "Collie client connect to " << host_vec[i] << " failed";
          return -1;
        }
      }
      clients.push_back(c);
    }
    std::vector<std::thread> client_threads;
    if (FLAGS_ctrl)
      for (auto c : clients)
        client_threads.push_back(
            std::thread(&Collie::rdma_context::MeasureThp, c));
    else
      for (auto c : clients)
        client_threads.push_back(
            std::thread(&Collie::rdma_context::ClientDatapath, c));
    for (auto &t: client_threads)
      t.join();
    return 0;
  }
  return 0;
}