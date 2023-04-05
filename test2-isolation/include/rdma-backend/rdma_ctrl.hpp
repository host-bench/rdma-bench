// MIT License

// Copyright (c) 2022 Duke University. All rights reserved.
// Copyright (c) 2021 ByteDance Inc. All rights reserved.

// See LICENSE for license information

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <infiniband/verbs.h>
#include <malloc.h>
#include <sys/time.h>
#include <time.h>

#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

DECLARE_int32(shm_key);

enum TestType {
  TestContext = 0,
  TestPd,
  TestCq,
  TestQp,
  TestMr,
  TestErr,
};

TestType StrToType(std::string const &str);

uint64_t Now64();  // return current second;

class IPCShm {
  int _shmid = 0;
  void *_shm = nullptr;
  int _thread_num = 0;

  std::mutex _lock;
  volatile int _ready_num;
  volatile int _phase = -1;

 public:
  IPCShm(int thread_num) : _thread_num(thread_num) { _ready_num = 0; }
  int ShmInit();
  void ShmClear();
  void NotifyVictim(int val);
  bool CheckShm();
  bool Update(int phase) {
    if (phase == _phase) {
      _lock.lock();
      _ready_num++;
      if (_ready_num == _thread_num) {
        LOG(INFO) << "Update() _phase is " << _phase;
        NotifyVictim(_phase);
        _phase++;
        // LOG(INFO) << "after notify victim _phase is " << _phase;
        _ready_num = 0;
      }
      _lock.unlock();
      return true;
    } 
    return false;
  }  // Each test thread call Update() to update the ready num
  int GetPhase() { return _phase; }
  void SetPhase(int phase) { _phase = phase; }
};

class Test {
 protected:
  uint64_t _alloc = 0;                 // phase for allocation
  uint64_t _middle = 0;                // phase for dealloc & alloc
  std::queue<void *> _res_queue;  // resource queue
                                  //  volatile int *_notify = nullptr;
  int _num_of_devices = 0;
  struct ibv_device **_device_list = nullptr;
  int _phase = 0;

 public:
  Test(int alloc, int middle);

  virtual ~Test();
  struct ibv_device *InitDevice(std::string ibdev);
  struct ibv_context *InitContext(std::string ibdev);
  struct ibv_pd *InitPd(struct ibv_context *ctx);
  int GetPhase() { return _phase; }
  void UpdatePhase() { _phase++; LOG(INFO) << "Phase Updated"; }
  virtual void AllocPhase() = 0;
  virtual void MiddlePhase() = 0;
  virtual void DeallocPhase() = 0;
};

class CtxTest : public Test {
 private:
  struct ibv_device *_device = nullptr;

 public:
  CtxTest(int alloc, int middle, std::string ibdev);
  void AllocPhase();
  void MiddlePhase();
  void DeallocPhase();
};

class PdTest : public Test {
 private:
  struct ibv_context *_ctx = nullptr;
  // TODO: PD can be allocated from different contexts.
  //      Ignore temporarily because contexts can be easily controlled: e.g.,
  //      each tenant only access one.
 public:
  PdTest(int alloc, int middle, std::string ibdev);
  void AllocPhase();
  void MiddlePhase();
  void DeallocPhase();
};

class CqTest : public Test {
 private:
  struct ibv_context *_ctx = nullptr;
  // Cq has ibv_create_cq parameters
  int _num_cqe = 1;
  // struct ibv_comp_channel *_channel = nullptr;
  // void *_context = nullptr;
  // int _comp_vec = 0;

 public:
  CqTest(int alloc, int middle, std::string ibdev, int num_cqe);
  void AllocPhase();
  void MiddlePhase();
  void DeallocPhase();
};

class QpTestParam {
 public:
  int num_pd;
  int num_send_cq;
  int num_recv_cq;
  int send_cqe;
  int recv_cqe;
  struct ibv_qp_cap cap;
  enum ibv_qp_type type;
  void Print() {
    LOG(INFO) << "All QPs are type of " << type;
    LOG(INFO) << "Number of PD for all QPs " << num_pd;
    LOG(INFO) << "Number of Send CQ for all QPs " << num_send_cq;
    LOG(INFO) << "Number of Recv CQ for all QPs " << num_recv_cq;
    LOG(INFO) << "Number of send_cqe per CQ is " << send_cqe;
    LOG(INFO) << "Number of recv_cqe per CQ is " << recv_cqe;
    LOG(INFO) << "Capacity:";
    LOG(INFO) << "    max_send_wr  " << cap.max_send_wr;
    LOG(INFO) << "    max_recv_wr  " << cap.max_recv_wr;
    LOG(INFO) << "    max_send_sge  " << cap.max_send_sge;
    LOG(INFO) << "    max_recv_sge  " << cap.max_recv_sge;
    LOG(INFO) << "    max_inline_data  " << cap.max_inline_data;
  }
};

class QpTest : public Test {
 private:
  std::vector<struct ibv_pd *> _pds;
  std::vector<struct ibv_cq *> _send_cqs;
  std::vector<struct ibv_cq *> _recv_cqs;
  struct ibv_qp_cap _cap;
  enum ibv_qp_type _type;

 public:
  QpTest(int alloc, int middle, std::string ibdev, QpTestParam param);
  void AllocPhase();
  void MiddlePhase();
  void DeallocPhase();

  // TODO: modify test (set up connection or allocate address handler)
};

class MrTestParam {
 public:
  int num_pd;
  int num_buffer;
  int size;
  bool memalign;
  int access;
  void Print() {
    LOG(INFO) << "Number of PD used by these mr " << num_pd;
    LOG(INFO) << "Number of buffer used by these mr " << num_buffer;
    LOG(INFO) << "Size of each buffer used by these mr " << size;
    LOG(INFO) << "Memalign ?  " << memalign;
    LOG(INFO) << "Access: ";
    LOG(INFO) << "  IBV_ACCESS_LOCAL_WRITE ? "
              << bool(access & IBV_ACCESS_LOCAL_WRITE);
    LOG(INFO) << "  IBV_ACCESS_REMOTE_READ ? "
              << bool(access & IBV_ACCESS_REMOTE_READ);
    LOG(INFO) << "  IBV_ACCESS_REMOTE_WRITE ? "
              << bool(access & IBV_ACCESS_REMOTE_WRITE);
    LOG(INFO) << "  IBV_ACCESS_REMOTE_ATOMIC ? "
              << bool(access & IBV_ACCESS_REMOTE_ATOMIC);
  }
};

class MrTest : public Test {
 private:
  std::vector<struct ibv_pd *> _pds;
  std::vector<char *> _buffers;
  int _num_pd;
  int _num_buffer;
  int _size;
  int _access;

 public:
  MrTest(int alloc, int middle, std::string ibdev, MrTestParam param);
  void AllocPhase();
  void MiddlePhase();
  void DeallocPhase();
};
