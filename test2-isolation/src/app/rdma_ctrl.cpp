// MIT License

// Copyright (c) 2022 Duke University. All rights reserved.

// See LICENSE for license information

#include "rdma_ctrl.hpp"

#include <infiniband/verbs.h>
#include <sys/shm.h>

constexpr int kShmVictimReady = 0xdeadbeef;
constexpr int kShmStart = 0;
constexpr int kShmPhase1 = 1;
constexpr int kShmPhase2 = 2;
constexpr int kShmPhase3 = 3;
constexpr int kBatchCtrl = 16;

// Use period instead of iterations
DEFINE_int32(alloc, 10, "Alloc phase execute for $alloc seconds.");
DEFINE_int32(middle, 10, "Middle phase execute for $alloc seconds.");
// DEFINE_int32(alloc_iters, 100000,
//              "The allocation iters, should be less than limit");
// DEFINE_int32(middle_iters, 100000, "The middle iters");
DEFINE_int32(thread, 1, "Number of threads");
DEFINE_int32(shm_key, 0x20220222,
             "The shared memory key between test program and victim app");
DEFINE_string(dev, "mlx5_0", "The IB device to use");
DEFINE_string(test, "", "Test type: context, mr, pd, qp, cq");
DEFINE_bool(notify, false, "Notify the victim application through Shared Memory (shm_key)");

// For individual test parameters
// CQ:
DEFINE_int32(num_cqe, 1, "The number of cqe for a CQ");

// QP:
DEFINE_int32(num_send_cq, 1, "The number of send cq for all QPs");
DEFINE_int32(num_recv_cq, 1, "The number of recv cq for all QPs");
DEFINE_int32(qp_type, 2, "QP Type: 2 for RC, 3 for UC, 4 for UD. ");
DEFINE_int32(num_pd, 1, "The number of pd for all QPs");
DEFINE_int32(max_send_wr, 16, "The number of max send work requests");
DEFINE_int32(max_recv_wr, 16, "The number of max recv work requests");
DEFINE_int32(max_send_sge, 16, "The number of max SGE of a send work requests");
DEFINE_int32(max_recv_sge, 16, "The number of max SGE of a recv work requests");
DEFINE_int32(max_inline_data, 64,
             "The number of max SGE of a recv work requests");

// MR:
// num_pd already defined
DEFINE_int32(mr_size, 65536, "The size of each buffer for MR tets");
DEFINE_int32(num_buffer, 1, "The number of buffer to be registered.");
DEFINE_bool(memalign, true, "memalign instead of malloc");

TestType StrToType(std::string const &str) {
  if (str == "context") return TestContext;
  if (str == "pd") return TestPd;
  if (str == "cq") return TestCq;
  if (str == "mr") return TestMr;
  if (str == "qp") return TestQp;
  return TestErr;
}

uint64_t Now64() {
  struct timespec tv;
  int res = clock_gettime(CLOCK_REALTIME, &tv);
  if (res == -1) {
    LOG(FATAL) << "clock_gettime failed: " << strerror(errno);
    return 0;
  }
  return (uint64_t)tv.tv_sec;
}

void IPCShm::ShmClear() {
  if (shmdt(_shm)) {
    PLOG(ERROR) << " shmdt() failed";
  }
  if (shmctl(_shmid, IPC_RMID, nullptr)) {
    PLOG(ERROR) << "shmctl() failed";
  }
}

int IPCShm::ShmInit() {
  void *shm = nullptr;
  int shmid = shmget((key_t)FLAGS_shm_key, sizeof(int), 0666 | IPC_CREAT);
  if (shmid < 0) {
    PLOG(ERROR) << __PRETTY_FUNCTION__ << " shmget failed";
    return shmid;
  }
  shm = shmat(shmid, 0, 0);
  if (shm == (void *)-1) {
    PLOG(ERROR) << __PRETTY_FUNCTION__ << " shmat failed";
    return -1;
  }
  LOG(INFO) << "Shared memory created.. attached at " << shm;
  _shmid = shmid;
  _shm = shm;
  return 0;
}

void IPCShm::NotifyVictim(int val) {
  volatile int *notify = (int *)_shm;
  *notify = val;
  sleep(1);  // TODO: allow victim to receive the notification.
}

bool IPCShm::CheckShm() {
  volatile int *notify = (int *)_shm;
  if (notify && *notify == kShmVictimReady) return true;
// debuf
  return false;
}

Test::Test(int alloc, int middle) : _alloc(alloc), _middle(middle) {}

Test::~Test() {}

struct ibv_device *Test::InitDevice(std::string ibdev) {
  if (!_device_list) {
    _device_list = ibv_get_device_list(&_num_of_devices);
    if (!_device_list || _num_of_devices <= 0) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed for ibv_get_device_list";
      return nullptr;
    }
  }
  for (int i = 0; i < _num_of_devices; i++) {
    if (strncmp(ibv_get_device_name(_device_list[i]), ibdev.c_str(),
                ibdev.size()) == 0) {
      return _device_list[i];
    }
  }
  LOG(ERROR) << __PRETTY_FUNCTION__ << " no such device: " << ibdev;
  return nullptr;
}

struct ibv_context *Test::InitContext(std::string ibdev) {
  auto device = InitDevice(ibdev);
  if (!device) {
    LOG(ERROR) << __PRETTY_FUNCTION__ << " getting device failed";
    return nullptr;
  }
  auto ctx = ibv_open_device(device);
  if (!ctx) {
    PLOG(ERROR) << " ibv_open_device() failed";
    return nullptr;
  }
  return ctx;
}

struct ibv_pd *Test::InitPd(struct ibv_context *ctx) {
  if (!ctx) {
    LOG(ERROR) << __PRETTY_FUNCTION__ << " getting context failed";
    return nullptr;
  }
  auto pd = ibv_alloc_pd(ctx);
  if (!ctx) {
    PLOG(ERROR) << " ibv_alloc_pd() failed";
    return nullptr;
  }
  return pd;
}

CtxTest::CtxTest(int alloc, int middle, std::string ibdev)
    : Test(alloc, middle) {
  _device = InitDevice(ibdev);
  if (!_device) {
    LOG(ERROR) << __PRETTY_FUNCTION__ << " getting device failed";
    exit(-1);
  }
}

void CtxTest::AllocPhase() {
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _alloc) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto ctx = ibv_open_device(_device);
      if (!ctx) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " contexts";
        flag = true;
        break;
      }
      _res_queue.push(ctx);
    }
    if (flag) break;
  }
}

void CtxTest::MiddlePhase() {
  // Dealloc one for capacity
  if (!_res_queue.empty()) {
    struct ibv_context *ctx = (struct ibv_context *)_res_queue.front();
    if (ibv_close_device(ctx)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " close the first context failed";
      return;
    }
    _res_queue.pop();
  }
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _middle) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto ctx = ibv_open_device(_device);
      if (!ctx) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " contexts";
        flag = true;
        break;
      }
      if (ibv_close_device(ctx)) {
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_close_device failed";
        flag = true;
        break;
      }
    }
    if (flag) break;
  }
}

void CtxTest::DeallocPhase() {
  while (!_res_queue.empty()) {
    struct ibv_context *ctx = (struct ibv_context *)_res_queue.front();
    _res_queue.pop();
    if (ibv_close_device(ctx)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_close_device failed";
    }
  }
}

PdTest::PdTest(int alloc, int middle, std::string ibdev) : Test(alloc, middle) {
  _ctx = InitContext(ibdev);
  if (!_ctx) {
    LOG(ERROR) << __PRETTY_FUNCTION__ << " getting context failed";
    exit(1);
  }
}

void PdTest::AllocPhase() {
  
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _alloc) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto pd = ibv_alloc_pd(_ctx);
      if (!pd) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " pd";
        flag = true;
        break;
      }
      _res_queue.push(pd);
    }
    if (flag) break;
  }
  
}

void PdTest::MiddlePhase() {
  
  if (!_res_queue.empty()) {
    struct ibv_pd *pd = (struct ibv_pd *)_res_queue.front();
    if (ibv_dealloc_pd(pd)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " close the first pd failed";
      return;
    }
    _res_queue.pop();
  }
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _middle) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto pd = ibv_alloc_pd(_ctx);
      if (!pd) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " pd";
        flag = true;
        break;
      }
      if (ibv_dealloc_pd(pd)) {
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_dealloc_pd failed";
        flag = true;
        break;
      }
    }
    if (flag) break;
  }
  
}

void PdTest::DeallocPhase() {
  
  while (!_res_queue.empty()) {
    struct ibv_pd *pd = (struct ibv_pd *)_res_queue.front();
    _res_queue.pop();
    if (ibv_dealloc_pd(pd)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_dealloc_pd failed";
    }
  }
  
}

CqTest::CqTest(int alloc, int middle, std::string ibdev, int num_cqe)
    : Test(alloc, middle), _num_cqe(num_cqe) {
  _ctx = InitContext(ibdev);
  if (!_ctx) {
    LOG(ERROR) << __PRETTY_FUNCTION__ << " getting context";
    exit(1);
  }
}

void CqTest::AllocPhase() {
  
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _alloc) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto cq = ibv_create_cq(_ctx, _num_cqe, nullptr, nullptr, 0);
      if (!cq) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " cq";
        flag = true;
        break;
      }
      _res_queue.push(cq);
    }
    if (flag) break;
  }
  
}

void CqTest::MiddlePhase() {
  if (!_res_queue.empty()) {
    struct ibv_cq *cq = (struct ibv_cq *)_res_queue.front();
    if (ibv_destroy_cq(cq)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " close the first cq failed";
      return;
    }
    _res_queue.pop();
  }
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _middle) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto cq = ibv_create_cq(_ctx, _num_cqe, nullptr, nullptr, 0);
      if (!cq) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " cq";
        flag = true;
        break;
      }
      if (ibv_destroy_cq(cq)) {
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_destroy_cq failed";
        flag = true;
        break;
      }
    }
    if (flag) break;
  }
  
}

void CqTest::DeallocPhase() {
  
  while (!_res_queue.empty()) {
    struct ibv_cq *cq = (struct ibv_cq *)_res_queue.front();
    _res_queue.pop();
    if (ibv_destroy_cq(cq)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_destroy_cq failed";
    }
  }
  
}

QpTest::QpTest(int alloc, int middle, std::string ibdev, QpTestParam param)
    : Test(alloc, middle),  _cap(param.cap), _type(param.type) {
  param.Print();
  auto ctx = InitContext(ibdev);  // All contexts here share the same ctx;
  if (param.num_pd < 1 || param.num_send_cq < 1 || param.num_recv_cq < 1) {
    LOG(ERROR) << __PRETTY_FUNCTION__
               << " qp's should allocate send/recv cq and pd";
    exit(1);
  }
  for (int i = 0; i < param.num_pd; i++) {
    auto pd = InitPd(ctx);
    if (!pd) {
      LOG(ERROR) << __PRETTY_FUNCTION__ << " getting pd failed";
      exit(1);
    }
    _pds.push_back(pd);
  }
  for (int i = 0; i < param.num_send_cq; i++) {
    auto send_cq = ibv_create_cq(ctx, param.send_cqe, nullptr, nullptr, 0);
    if (!send_cq) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " send_cq ibv_create_cq() failed ";
      exit(1);
    }
    _send_cqs.push_back(send_cq);
    auto recv_cq = ibv_create_cq(ctx, param.recv_cqe, nullptr, nullptr, 0);
    if (!recv_cq) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " recv_cq ibv_create_cq() failed ";
      exit(1);
    }
    _recv_cqs.push_back(recv_cq);
  }
}

void QpTest::AllocPhase() {
  
  size_t send_cq_idx = 0, recv_cq_idx = 0, pd_idx = 0;
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _alloc) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      struct ibv_qp_init_attr qp_init_attr;
      qp_init_attr.cap = _cap;
      qp_init_attr.qp_context = nullptr;
      qp_init_attr.qp_type = IBV_QPT_RC;
      qp_init_attr.send_cq = _send_cqs[send_cq_idx++];
      qp_init_attr.recv_cq = _recv_cqs[recv_cq_idx++];
      qp_init_attr.sq_sig_all = 1;
      qp_init_attr.srq = nullptr;
      auto qp = ibv_create_qp(_pds[pd_idx++], &qp_init_attr);
      if (!qp) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " qp";
        flag = true;
        break;
      }
      _res_queue.push(qp);
      if (send_cq_idx == _send_cqs.size()) send_cq_idx = 0;
      if (recv_cq_idx == _recv_cqs.size()) recv_cq_idx = 0;
      if (pd_idx == _pds.size()) pd_idx = 0;
    }
    if (flag) break;
  }
  
}

void QpTest::MiddlePhase() {
  
  if (!_res_queue.empty()) {
    struct ibv_qp *qp = (struct ibv_qp *)_res_queue.front();
    if (ibv_destroy_qp(qp)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " close the first qp failed";
      return;
    }
    _res_queue.pop();
  }
  size_t send_cq_idx = 0, recv_cq_idx = 0, pd_idx = 0;
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _middle) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      struct ibv_qp_init_attr qp_init_attr;
      qp_init_attr.cap = _cap;
      qp_init_attr.qp_context = nullptr;
      qp_init_attr.qp_type = IBV_QPT_RC;
      qp_init_attr.send_cq = _send_cqs[send_cq_idx++];
      qp_init_attr.recv_cq = _recv_cqs[recv_cq_idx++];
      qp_init_attr.sq_sig_all = 1;
      qp_init_attr.srq = nullptr;
      auto qp = ibv_create_qp(_pds[pd_idx++], &qp_init_attr);
      if (!qp) {
        // alloc failed
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when alloc " << i
                    << " qp";
        flag = true;
        break;
      }
      if (ibv_destroy_qp(qp)) {
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_destroy_qp failed";
        flag = true;
        break;
      }
      if (send_cq_idx == _send_cqs.size()) send_cq_idx = 0;
      if (recv_cq_idx == _recv_cqs.size()) recv_cq_idx = 0;
      if (pd_idx == _pds.size()) pd_idx = 0;
    }
    if (flag) break;
  }
  
}

void QpTest::DeallocPhase() {
  
  while (!_res_queue.empty()) {
    struct ibv_qp *qp = (struct ibv_qp *)_res_queue.front();
    _res_queue.pop();
    if (ibv_destroy_qp(qp)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_destroy_qp failed";
    }
  }
  
}

MrTest::MrTest(int alloc, int middle, std::string ibdev, MrTestParam param)
    : Test(alloc, middle), _size(param.size), _access(param.access){
  param.Print();
  auto ctx = InitContext(ibdev);  // All contexts here share the same ctx;
  if (param.num_pd < 1 || param.num_buffer < 1) {
    LOG(ERROR) << __PRETTY_FUNCTION__ << " MR should allocate pd and buffer";
    exit(1);
  }
  for (int i = 0; i < param.num_pd; i++) {
    auto pd = InitPd(ctx);
    if (!pd) {
      LOG(ERROR) << __PRETTY_FUNCTION__ << " getting pd failed";
      exit(1);
    }
    _pds.push_back(pd);
  }
  for (int i = 0; i < param.num_buffer; i++) {
    char *buffer = nullptr;
    if (param.memalign)
      buffer = (char *)memalign(sysconf(_SC_PAGESIZE), _size);
    else
      buffer = (char *)malloc(_size);
    if (!buffer) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " allocate buffer failed";
      exit(1);
    }
    _buffers.push_back(buffer);
  }
}

void MrTest::AllocPhase() {
  
  size_t pd_idx = 0, buf_idx = 0;
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _alloc) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto mr = ibv_reg_mr(_pds[pd_idx++], _buffers[buf_idx++], _size, _access);
      if (!mr) {
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when create " << i
                    << " mr ";
        flag = true;
        break;
      }
      _res_queue.push(mr);
      // overflow check
      if (pd_idx == _pds.size()) pd_idx = 0;
      if (buf_idx == _buffers.size()) buf_idx = 0;
    }
    if (flag) break;
  }
  
}

void MrTest::MiddlePhase() {
  
  if (!_res_queue.empty()) {
    struct ibv_mr *mr = (struct ibv_mr *)_res_queue.front();
    if (ibv_dereg_mr(mr)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " close the first mr failed";
      return;
    }
    _res_queue.pop();
  }
  size_t buf_idx = 0, pd_idx = 0;
  auto start_ts = Now64();
  while (true) {
    auto cur_ts = Now64();
    bool flag = false;
    if (cur_ts - start_ts >= _middle) break;
    for (int i = 0; i < kBatchCtrl; i++) {
      auto mr = ibv_reg_mr(_pds[pd_idx++], _buffers[buf_idx++], _size, _access);
      if (!mr) {
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " failed when create " << i
                    << " mr ";
        flag = true;
        break;
      }
      if (ibv_dereg_mr(mr)) {
        PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_dereg_mrfailed";
        flag = true;
        break;
      }
      // overflow check
      if (pd_idx == _pds.size()) pd_idx = 0;
      if (buf_idx == _buffers.size()) buf_idx = 0;
    }
    if (flag) break;
  }
  
}

void MrTest::DeallocPhase() {
  
  while (!_res_queue.empty()) {
    struct ibv_mr *mr = (struct ibv_mr *)_res_queue.front();
    _res_queue.pop();
    if (ibv_dereg_mr(mr)) {
      PLOG(ERROR) << __PRETTY_FUNCTION__ << " ibv_dereg_mr failed";
    }
  }
  
}

QpTestParam QpParamFromFlags() {
  QpTestParam param;
  param.send_cqe = param.recv_cqe = FLAGS_num_cqe;
  param.num_pd = FLAGS_num_pd;
  param.num_recv_cq = FLAGS_num_recv_cq;
  param.num_send_cq = FLAGS_num_send_cq;
  param.type = (enum ibv_qp_type)FLAGS_qp_type;
  param.cap.max_send_wr = FLAGS_max_send_wr;
  param.cap.max_send_sge = FLAGS_max_send_sge;
  param.cap.max_recv_wr = FLAGS_max_recv_wr;
  param.cap.max_recv_sge = FLAGS_max_recv_sge;
  param.cap.max_inline_data = FLAGS_max_inline_data;
  return param;
}

MrTestParam MrParamFromFlags() {
  MrTestParam param;
  param.num_pd = FLAGS_num_pd;
  param.num_buffer = FLAGS_num_buffer;
  param.size = FLAGS_mr_size;
  param.memalign = FLAGS_memalign;
  param.access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                 IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
  return param;
}

Test *NewTest() {
  Test *test = nullptr;
  switch (StrToType(FLAGS_test)) {
    case TestContext:
      test = new CtxTest(FLAGS_alloc, FLAGS_middle, FLAGS_dev);
      break;
    case TestPd:
      test = new PdTest(FLAGS_alloc, FLAGS_middle, FLAGS_dev);
      break;
    case TestCq:
      test = new CqTest(FLAGS_alloc, FLAGS_middle, FLAGS_dev, FLAGS_num_cqe);
      break;
    case TestQp:
      test =
          new QpTest(FLAGS_alloc, FLAGS_middle, FLAGS_dev, QpParamFromFlags());
      break;
    case TestMr:
      test =
          new MrTest(FLAGS_alloc, FLAGS_middle, FLAGS_dev, MrParamFromFlags());
      break;
    default:
      LOG(ERROR) << "Unsupporterd Test type.";
      break;
  }
  return test;
}

void TestThread(Test *test, IPCShm *ipc) {
  while (!ipc->Update(test->GetPhase()))
    ;
  while (ipc->GetPhase() < test->GetPhase())
    ;  // wait for all threads finished
  test->AllocPhase();
  test->UpdatePhase();
  while (!ipc->Update(test->GetPhase()))
    ;
  while (ipc->GetPhase() < test->GetPhase())
    ;  // wait for all threads finished
  test->MiddlePhase();
  test->UpdatePhase();
  while (!ipc->Update(test->GetPhase()))
    ;
  while (ipc->GetPhase() < test->GetPhase())
    ;
  test->DeallocPhase();
  test->UpdatePhase();
  while (!ipc->Update(test->GetPhase()))
    ;
  return;
}

void IPCThread(IPCShm *ipc) {
  ipc->ShmInit();
  if (FLAGS_notify)
    while (!ipc->CheckShm()) ;  // wait for victim to start;
  ipc->NotifyVictim(kShmStart);
  ipc->SetPhase(0);
  while (ipc->GetPhase() < kShmPhase3) ;
  return ;
}

// This function should be
void Run(std::vector<Test *> test_set) {
  // Init
  IPCShm *ipc = new IPCShm(FLAGS_thread);
  std::thread ipc_thread = std::thread(&IPCThread, ipc);
  std::vector<std::thread> test_threads;
  for (size_t i = 0; i < test_set.size(); i++) {
    test_threads.push_back(std::thread(&TestThread, test_set[i], ipc));
  }
  for (size_t i = 0; i < test_set.size(); i++) {
    test_threads[i].join();
  }
  ipc_thread.join();
}

int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::vector<Test *> test_set;
  for (int i = 0; i < FLAGS_thread; i++) test_set.push_back(NewTest());
  Run(test_set);
}