#include <gflags/gflags.h>
#include <glog/logging.h>
#include <infiniband/verbs.h>
#include <malloc.h>
#include <sys/time.h>
#include <time.h>

#include <iostream>
#include <thread>
#include <vector>
#include <infiniband/verbs.h>

DEFINE_int32(context_num, 4, "Number of contexts to create\n");
DEFINE_int32(pd_num, 16, "Number of Pd to create\n");
DEFINE_int32(cq_num, 16, "Number of Cq to create\n");
DEFINE_int32(cqe_num, 16, "Number of CQE to create\n");
DEFINE_int32(qp_num, 16, "Number of QP to create\n");
DEFINE_int32(wr_num, 128, "Number of WR to create\n");
DEFINE_int32(sge_num, 1, "Number of SGE to create\n");
DEFINE_int32(mr_num, 16, "Number of MR to create\n");
DEFINE_int32(mr_min_size, 1, "Size of MR to create\n");
DEFINE_int32(mr_max_size, 65536, "Size of MR to create\n");
DEFINE_string(ib_dev, "mlx5_0", "IB device name\n");


uint64_t Now64() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

struct ibv_context *TestContext(const std::string name, int nums) {
    int _n = 0;
    struct ibv_device **dev_list = ibv_get_device_list(&_n);
    if (!dev_list || _n <= 0) {
        LOG(ERROR) << "Failed to get IB devices list";
        return NULL;
    }
    struct ibv_device *dev;
    for (int i = 0; i < _n; i++) {
        dev = dev_list[i];
        if (strncmp(ibv_get_device_name(dev), name.c_str(), name.size()) == 0) {
            break;
        }
        dev = NULL;
    }
    if (!dev) {
        LOG(ERROR) << "Failed to find IB device " << name;
        return NULL;
    }
    std::vector<struct ibv_context *> _contexts;
    uint64_t before = Now64();
    for (int i = 0; i < nums; i++) {
        struct ibv_context *ctx = ibv_open_device(dev);
        if (!ctx) {
            LOG(ERROR) << "Failed to open device " << name;
            return NULL;
        }
        _contexts.push_back(ctx);
    }
    uint64_t after = Now64();
    LOG(INFO) << "Open " << nums << " devices cost " << (after - before) << " us";
    LOG(INFO) << "Average cost " << (after - before) * 1.0 / nums << " us";
    uint64_t before_tail = Now64();
    struct ibv_context *ctx = ibv_open_device(dev);
    if (!ctx) {
        LOG(ERROR) << "Failed to open device " << name;
        return NULL;
    }
    uint64_t after_tail = Now64();
    LOG(INFO) << "Tail " << nums << " Open device cost " << (after_tail - before_tail) << " us";
    for (auto ctx : _contexts) {
        ibv_close_device(ctx);
    }
    ibv_free_device_list(dev_list);
    return ctx;
}

struct ibv_pd *TestPd(struct ibv_context *context, int nums) {
    std::vector<struct ibv_pd *> _pds;
    uint64_t before = Now64();
    for (int i = 0; i < nums; i++) {
        struct ibv_pd *pd = ibv_alloc_pd(context);
        if (!pd) {
            LOG(ERROR) << "Failed to allocate PD";
            return NULL;
        }
        _pds.push_back(pd);
    }
    uint64_t after = Now64();
    LOG(INFO) << "Allocate " << nums << " Pd cost " << (after - before) << " us";
    LOG(INFO) << "Average cost " << (after - before) * 1.0 / nums << " us";
    uint64_t before_tail = Now64();
    struct ibv_pd *pd = ibv_alloc_pd(context);
    if (!pd) {
        LOG(ERROR) << "Failed to allocate PD";
        return NULL;
    }
    uint64_t after_tail = Now64();
    LOG(INFO) << "Tail " << nums << " Allocate Pd cost " << (after_tail - before_tail) << " us";
    for (auto _pd : _pds) {
        ibv_dealloc_pd(_pd);
    }

    return pd;
}

struct ibv_cq *TestCq(struct ibv_context *context, int nums, int num_cqe) {
    std::vector<struct ibv_cq *> _cqs;
    uint64_t before = Now64();
    for (int i = 0; i < nums; i++) {
        struct ibv_cq *cq = ibv_create_cq(context, num_cqe, NULL, NULL, 0);
        if (!cq) {
            LOG(ERROR) << "Failed to create CQ";
            return NULL;
        }
        _cqs.push_back(cq);
    }
    uint64_t after = Now64();
    LOG(INFO) << "Create " << nums << " Cq (" << num_cqe << " cqes) cost " << (after - before) << " us";
    LOG(INFO) << "Average cost " << (after - before) * 1.0 / nums << " us";
    uint64_t before_tail = Now64();
    struct ibv_cq *cq = ibv_create_cq(context, num_cqe, NULL, NULL, 0);
    if (!cq) {
        LOG(ERROR) << "Failed to create CQ";
        return NULL;
    }
    uint64_t after_tail = Now64();
    LOG(INFO) << "Tail " << nums << " Create CQ cost " << (after_tail - before_tail) << " us";
    for (auto _cq : _cqs) {
        ibv_destroy_cq(_cq);
    }
    return cq;
}

struct ibv_qp *TestQp(struct ibv_pd *pd, struct ibv_cq *cq, int nums, int num_wr, int num_sge) {
    std::vector<struct ibv_qp *> _qps;
    uint64_t before = Now64();
    for (int i = 0; i < nums; i++) {
        struct ibv_qp_init_attr qp_init_attr;
        memset(&qp_init_attr, 0, sizeof(qp_init_attr));
        qp_init_attr.send_cq = cq;
        qp_init_attr.recv_cq = cq;
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.cap.max_send_wr = num_wr;
        qp_init_attr.cap.max_recv_wr = num_wr;
        qp_init_attr.cap.max_send_sge = num_sge;
        qp_init_attr.cap.max_recv_sge = num_sge;
        struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
        if (!qp) {
            LOG(ERROR) << "Failed to create QP";
            return NULL;
        }
        _qps.push_back(qp);
    }
    uint64_t after = Now64();
    LOG(INFO) << "Create " << nums << " Qp (" << num_wr << " send/recv wrs) cost " << (after - before) << " us";
    LOG(INFO) << "Average cost " << (after - before) * 1.0 / nums << " us";
    uint64_t before_tail = Now64();
    struct ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr = num_wr;
    qp_init_attr.cap.max_recv_wr = num_wr;
    qp_init_attr.cap.max_send_sge = num_sge;
    qp_init_attr.cap.max_recv_sge = num_sge;
    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        LOG(ERROR) << "Failed to create QP";
        return NULL;
    }
    uint64_t after_tail = Now64();
    LOG(INFO) << "Tail " << nums << " Create QP cost " << (after_tail - before_tail) << " us";
    for (auto _qp : _qps) {
        ibv_destroy_qp(_qp);
    }
    return qp;
}

struct ibv_mr *TestMr(struct ibv_pd *pd, int nums, int size) {
    std::vector<struct ibv_mr *> _mrs;
    uint64_t before = Now64();
    for (int i = 0; i < nums; i++) {
        char *buf = (char *)malloc(size);
        struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
        if (!mr) {
            LOG(ERROR) << "Failed to register MR";
            return NULL;
        }
        _mrs.push_back(mr);
    }
    uint64_t after = Now64();
    LOG(INFO) << "Register " << nums << " Mr size (" << size << " bytes) cost " << (after - before) << " us";
    LOG(INFO) << "Average cost " << (after - before) * 1.0 / nums << " us";
    char *buf = (char*) malloc(size);
    uint64_t before_tail = Now64();
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!mr) {
        LOG(ERROR) << "Failed to register MR";
        return NULL;
    }
    uint64_t after_tail = Now64();
    LOG(INFO) << "Tail " << nums << " Register MR cost " << (after_tail - before_tail) << " us";
    for (auto _mr : _mrs) {
        buf  = (char *)_mr->addr;
        ibv_dereg_mr(_mr);
        free(buf);
    }
    return mr;
}

int main(int argc, char **argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    struct ibv_context *context = TestContext(FLAGS_ib_dev, FLAGS_context_num);
    if (!context) {
        LOG(ERROR) << "Failed to get context";
        return -1;
    }
    std::cout << std::endl;
    struct ibv_pd *pd = TestPd(context, FLAGS_pd_num);
    if (!pd) {
        LOG(ERROR) << "Failed to get pd";
        return -1;
    }
    std::cout << std::endl;
    struct ibv_cq *cq = TestCq(context, FLAGS_cq_num, FLAGS_cqe_num);
    if (!cq) {
        LOG(ERROR) << "Failed to get cq";
        return -1;
    }
    std::cout << std::endl;
    struct ibv_qp *qp = TestQp(pd, cq, FLAGS_qp_num, FLAGS_wr_num, FLAGS_sge_num);
    if (!qp) {
        LOG(ERROR) << "Failed to get qp";
        return -1;
    }
    std::cout << std::endl;
    struct ibv_mr *mr = TestMr(pd, FLAGS_mr_num, FLAGS_mr_min_size);
    if (!mr) {
        LOG(ERROR) << "Failed to get mr";
        return -1;
    }
    std::cout << std::endl;
    mr = TestMr(pd, FLAGS_mr_num, FLAGS_mr_max_size);
    if (!mr) {
        LOG(ERROR) << "Failed to get mr";
        return -1;
    }
    std::cout << std::endl;
    // Destroy all resources are done by kernel. 
    return 0;
}