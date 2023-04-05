// MIT License

// Copyright (c) 2022 Duke University. All rights reserved.
// Copyright (c) 2021 ByteDance Inc. All rights reserved.

// See LICENSE for license information

#include "rdma_context.hpp"

#include <malloc.h>

#include <thread>
#include <sys/shm.h>
#include <algorithm>

#ifdef USE_CUDA
#define ASSERT(x)                                                                          \
    do {                                                                                   \
        if (!(x)) {                                                                        \
            fprintf(stdout, "Assertion \"%s\" failed at %s:%d\n", #x, __FILE__, __LINE__); \
        }                                                                                  \
    } while (0)

#define CUCHECK(stmt)                   \
    do {                                \
        CUresult result = (stmt);       \
        ASSERT(CUDA_SUCCESS == result); \
    } while (0)
#endif

namespace Collie {

constexpr int kStartSecs = 4; // running seconds before first phase.
constexpr int kShmVictimReady = 0xdeadbeef;
constexpr int kShmStart = 0;
constexpr int kShmPhase1 = 1;
constexpr int kShmPhase2 = 2;
constexpr int kShmPhase3 = 3;

//static int canExit = false;

std::vector<rdma_request> rdma_context::ParseRecvFromStr() {
    std::stringstream ss(FLAGS_receive);
    char c;
    int size;
    int sge_num;
    std::vector<rdma_request> requests;
    while (ss >> sge_num) {
        rdma_request req;
        for (int i = 0; i < sge_num; i++) {
            ss >> c >> size;
            struct ibv_sge sg;
            auto buf = PickNextBuffer(1);
            sg.addr = buf->addr_;
            sg.lkey = buf->lkey_;
            sg.length = size;
            req.sglist.push_back(sg);
        }
        req.sge_num = sge_num;
        requests.push_back(req);
    }
    return requests;
}

std::vector<rdma_request> rdma_context::ParseReqFromStr() {
    std::stringstream ss(FLAGS_request);
    char op;
    char c;
    int size;
    int sge_num;
    std::vector<rdma_request> requests;
    while (ss >> op >> c >> sge_num) {
        rdma_request req;
        if (op != 's' && FLAGS_qp_type == 4) {
            LOG(ERROR) << "UD does not support opcode other than SEND/RECV";
            exit(1);
        }
        if (op == 'r' && FLAGS_qp_type != 2) {
            LOG(ERROR) << "Only RC supports RDMA Read";
            exit(1);
        }
        switch (op) {
            case 'w':
                req.opcode = IBV_WR_RDMA_WRITE;
                if (FLAGS_imm_data) 
                    req.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
                break;
            case 'r':
                req.opcode = IBV_WR_RDMA_READ;
                break;
            case 's':
                req.opcode = IBV_WR_SEND;
                break;
            case 'f':
                req.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
                break;
            case 'c':
                req.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
                break;
            default:
                LOG(ERROR) << "Unsupported work request opcode";
                exit(1);
        }
        req.sge_num = sge_num;
        for (int i = 0; i < sge_num; i++) {
            ss >> c >> size;
            struct ibv_sge sge;
            auto buf = PickNextBuffer(0);
            sge.addr = buf->addr_;
            sge.length = size;
            sge.lkey = buf->lkey_;
            req.sglist.push_back(sge);
        }
        requests.push_back(req);
        if (ss.peek() == ',')
            ss.ignore();
    }
    return requests;
}

std::string rdma_context::GidToIP(const union ibv_gid &gid) {
    std::string ip = std::to_string(gid.raw[12]) + "." + std::to_string(gid.raw[13]) + "." + std::to_string(gid.raw[14]) + "." + std::to_string(gid.raw[15]);
    return ip;
}

int SetShmThread() {
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
    std::this_thread::sleep_for(std::chrono::milliseconds(kStartSecs * 2000)); // two seconds before run 
    volatile int *ipc_val = (int*) shm;
    *ipc_val = kShmVictimReady;
    int last_val = kShmVictimReady;
    while (true) {
        int val = *ipc_val;
        if (val != last_val) {
            last_val = val;
            switch(val) {
                case kShmStart:
                    LOG(INFO) << "======================   start   ===========================";
                    std::cout << "-1,-1" << std::endl;
                    break;
                case kShmPhase1:
                    LOG(INFO) << "===================== Phase 1 ends =========================";
                    std::cout << "-1,-1" << std::endl;
                    break;
                case kShmPhase2:
                    LOG(INFO) << "===================== Phase 2 ends =========================";
                    std::cout << "-1,-1" << std::endl;
                    break;
                case kShmPhase3:
                    LOG(INFO) << "===================== Phase 3 ends =========================";
                    std::cout << "-1,-1" << std::endl;
                    break;
                default:
                    LOG(ERROR) << "ShmVal Wrong!";
                    goto fail;
            }
        }
        if (last_val == kShmPhase3) {
            shmdt(shm);
            // Test over. Wait for 5 seconds to exit the whole process.
            std::this_thread::sleep_for(std::chrono::milliseconds(7000));
            //canExit = true;
        }
    }
    return 0;
    fail:
        shmdt(shm);
        return -1;
}

int rdma_context::Init() {
#ifdef USE_CUDA
    if (InitCuda() < 0) {
        LOG(ERROR) << "InitCuda() failed";
        return -1;
    }
#endif
    if (InitDevice() < 0) {
        LOG(ERROR) << "InitDevice() failed";
        return -1;
    }
    if (InitMemory() < 0) {
        LOG(ERROR) << "InitMemory() failed";
        return -1;
    }
    if (InitTransport() < 0) {
        LOG(ERROR) << "InitTransport() failed";
        return -1;
    }
    return 0;
}

int rdma_context::CheckDeviceParams() {
    if (ibv_query_device(this->ctx_, &this->device_attr_)) {
        PLOG(ERROR) << "ibv_query_device() failed";
        return -1;
    }
    if (kMaxSge > this->device_attr_.max_sge) {
        LOG(ERROR) << "kMaxSge is too large. It should be less than " << this->device_attr_.max_sge;
        LOG(ERROR) << "Please edit kMaxSge in rdma_context.h and recompile the code.";
        return -1;
    }
    if (FLAGS_send_wq_depth > this->device_attr_.max_qp_wr || FLAGS_recv_wq_depth > this->device_attr_.max_qp_wr) {
        LOG(ERROR) << "kSendWqDepth or kRecvWqDepth is too large. It should be less than " << this->device_attr_.max_qp_wr;
        LOG(ERROR) << "kSendWqDepth: " << FLAGS_send_wq_depth << ", kRecvWqDepth: " << FLAGS_recv_wq_depth;
        return -1;
    }
    return 0;
}

int rdma_context::InitDevice() {
    struct ibv_device *dev = nullptr;
    struct ibv_device **device_list = nullptr;
    int n;
    bool flag = false;
    device_list = ibv_get_device_list(&n);
    if (!device_list) {
        PLOG(ERROR) << "ibv_get_device_list() failed when initializing clients";
        return -1;
    }
    for (int i = 0; i < n; i++) {
        dev = device_list[i];
        if (!strncmp(ibv_get_device_name(dev), devname_.c_str(), strlen(devname_.c_str()))) {
            flag = true;
            break;
        }
    }
    if (!flag) {
        LOG(ERROR) << "We didn't find device " << devname_ << ". So we exit.";
    }
    this->ctx_ = ibv_open_device(dev);
    if (!ctx_) {
        PLOG(ERROR) << "ibv_open_device() failed";
        return -1;
    }
    ibv_free_device_list(device_list);
    if (CheckDeviceParams() < 0) {
        LOG(ERROR) << "CheckDeviceParams() failed";
        return -1;
    }
    send_cqs_.clear();
    recv_cqs_.clear();
    share_pd_ = FLAGS_share_pd;
    share_cq_ = FLAGS_share_cq;
    if (ibv_query_gid(ctx_, 1, local_gid_idx_, &local_gid_) < 0) {
        PLOG(ERROR) << "ibv_query_gid() failed";
        return -1;
    }
    local_ip_ = GidToIP(local_gid_);
    auto num_of_qps = InitIds();
    endpoints_.resize(num_of_qps, nullptr);
    struct ibv_port_attr port_attr;
    memset(&port_attr, 0, sizeof(port_attr));
    if (ibv_query_port(ctx_, 1, &port_attr)) {
        PLOG(ERROR) << "ibv_query_port() failed";
        exit(1);
    }
    lid_ = port_attr.lid;
    sl_ = port_attr.sm_sl;
    port_ = FLAGS_port;
    return 0;
}
int rdma_context::InitIds() {
    while (!ids_.empty()) {
        ids_.pop();
    }
    auto num_of_qps = num_of_hosts_ * num_per_host_;
    for (int i = 0; i < num_of_qps; i++) {
        ids_.push(i);
    }
    return num_of_qps;
}

#ifdef USE_CUDA
int rdma_context::InitCuda() {
    if (FLAGS_use_cuda == false)
        return 0;
    int cuda_pci_bus_id;
    int cuda_pci_device_id;
    int cuda_device_id = FLAGS_cuda_device_id;
    int index;
    CUdevice cu_device;

    printf("initializing CUDA\n");
    CUresult error = cuInit(0);
    if (error != CUDA_SUCCESS) {
        printf("cuInit(0) returned %d\n", error);
        perror("CuInit()");
        exit(1);
    }

    int deviceCount = 0;
    error = cuDeviceGetCount(&deviceCount);
    if (error != CUDA_SUCCESS) {
        printf("cuDeviceGetCount() returned %d\n", error);
        exit(1);
    }
    /* This function call returns 0 if there are no CUDA capable devices. */
    if (deviceCount == 0) {
        printf("There are no available device(s) that support CUDA\n");
        return 1;
    }
    if (cuda_device_id >= deviceCount) {
        fprintf(stderr, "No such device ID (%d) exists in system\n", cuda_device_id);
        return 1;
    }

    printf("Listing all CUDA devices in system:\n");
    for (index = 0; index < deviceCount; index++) {
        CUCHECK(cuDeviceGet(&cu_device, index));
        cuDeviceGetAttribute(&cuda_pci_bus_id, CU_DEVICE_ATTRIBUTE_PCI_BUS_ID, cu_device);
        cuDeviceGetAttribute(&cuda_pci_device_id, CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID, cu_device);
        printf("CUDA device %d: PCIe address is %02X:%02X\n", index, (unsigned int)cuda_pci_bus_id, (unsigned int)cuda_pci_device_id);
    }
    printf("\nPicking device No. %d\n", cuda_device_id);
    CUCHECK(cuDeviceGet(&cuDevice_, cuda_device_id));
    char name[128];
    CUCHECK(cuDeviceGetName(name, sizeof(name), cuda_device_id));
    printf("[pid = %d, dev = %d] device name = [%s]\n", getpid(), cuDevice_, name);
    printf("creating CUDA Ctx\n");

    /* Create context */
    error = cuCtxCreate(&cuContext_, CU_CTX_MAP_HOST, cuDevice_);
    if (error != CUDA_SUCCESS) {
        printf("cuCtxCreate() error=%d\n", error);
        return 1;
    }

    printf("making it the current CUDA Ctx\n");
    error = cuCtxSetCurrent(cuContext_);
    if (error != CUDA_SUCCESS) {
        printf("cuCtxSetCurrent() error=%d\n", error);
        return 1;
    }
    return 0;
}
#endif

int rdma_context::InitMemory() {
    // Allocate PD
    auto pd_num = share_pd_ ? 1 : FLAGS_qp_num;
    for (int i = 0; i < pd_num; i++) {
        auto pd = ibv_alloc_pd(ctx_);
        if (!pd) {
            PLOG(ERROR) << "ibv_alloc_pd() failed";
            return -1;
        }
        pds_.push_back(pd);
    }

    auto buf_size = FLAGS_buf_size;
    // Allocate Memory and Register them
    if ((enum ibv_qp_type)FLAGS_qp_type == IBV_QPT_UD) {
        buf_size += kUdAddition;
    }
    for (int i = 0; i < FLAGS_mr_num; i++) {
        auto region = new rdma_region(GetPd(i), buf_size, FLAGS_buf_num, FLAGS_memalign, 0);
        if (region->Mallocate(FLAGS_connect != "")) {
            LOG(ERROR) << "Region Memory allocation failed";
            break;
        }
        local_mempool_[0].push_back(region);
        
    }
    for (int i = 0; i < 1; i++) {
        auto region = new rdma_region(GetPd(i), buf_size, FLAGS_buf_num, FLAGS_memalign, 0);
//        region = new rdma_region(GetPd(i), buf_size, FLAGS_buf_num, FLAGS_memalign, 0);
        if (region->Mallocate(FLAGS_server)) {
            LOG(ERROR) << "Region Memory allocation failed";
            break;
        }
        local_mempool_[1].push_back(region);
    }

    // Allocate Send/Recv Completion Queue
    auto cqn = share_cq_ ? 1 : num_of_hosts_ * num_per_host_;
    for (int i = 0; i < cqn; i++) {
        union collie_cq send_cq;
        union collie_cq recv_cq;
        send_cq.cq = ibv_create_cq(ctx_, FLAGS_cq_depth / cqn, nullptr, nullptr, 0);
        if (!send_cq.cq) {
            PLOG(ERROR) << "ibv_create_cq() failed";
            return -1;
        }
        recv_cq.cq = ibv_create_cq(ctx_, FLAGS_cq_depth / cqn, nullptr, nullptr, 0);
        if (!recv_cq.cq) {
            PLOG(ERROR) << "ibv_create_cq() failed";
            return -1;
        }
        send_cqs_.push_back(send_cq);
        recv_cqs_.push_back(recv_cq);
    }
    return 0;
}

int rdma_context::InitTransport() {
    rdma_endpoint *ep = nullptr;
    while (!ids_.empty()) {
        auto id = ids_.front();
        ids_.pop();
        if (endpoints_[id])
            delete endpoints_[id];
        struct ibv_qp_init_attr qp_init_attr = MakeQpInitAttr(GetSendCq(id), GetRecvCq(id), FLAGS_send_wq_depth, FLAGS_recv_wq_depth);
        auto qp = ibv_create_qp(GetPd(id), &qp_init_attr);
        if (!qp) {
            PLOG(ERROR) << "ibv_create_qp() failed";
            delete ep;
            return -1;
        }
        ep = new rdma_endpoint(id, qp);
        ep->SetMaster(this);
        endpoints_[id] = ep;
    }
    return 0;
}

int rdma_context::Listen() {
    struct addrinfo *res, *t;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char *service;
    int sockfd = -1, err, n;
    int *connfd;
    if (asprintf(&service, "%d", port_) < 0)
        return -1;
    if (getaddrinfo(nullptr, service, &hints, &res)) {
        LOG(ERROR) << gai_strerror(n) << " for port " << port_;
        free(service);
        return -1;
    }
    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            n = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
            if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
                break;
            close(sockfd);
            sockfd = -1;
        }
    }
    freeaddrinfo(res);
    free(service);
    if (sockfd < 0) {
        LOG(ERROR) << "Couldn't listen to port " << port_;
        return -1;
    }
    LOG(INFO) << "About to listen on port " << port_;
    err = listen(sockfd, 1024);
    if (err) {
        PLOG(ERROR) << "listen() failed";
        return -1;
    }
    LOG(INFO) << "Server listen thread starts";
    while (true) {
        connfd = (int *)malloc(sizeof(int));
        *connfd = accept(sockfd, nullptr, 0);
        if (*connfd < 0) {
            PLOG(ERROR) << "Accept Error";
            break;
        }
        // [TODO] connection handler
        std::thread handler = std::thread(&rdma_context::AcceptHandler, this, *connfd);
        handler.detach();
        free(connfd);
    }
    // The loop shall never end.
    free(connfd);
    close(sockfd);
    return -1;
}

void rdma_context::SetEndpointInfo(rdma_endpoint *endpoint, struct connect_info *info) {
    switch (endpoint->GetType()) {
        case IBV_QPT_UD:
            endpoint->SetLid(ntohl(info->info.channel.dlid));
            endpoint->SetSl(ntohl(info->info.channel.sl)); // Fall through
        case IBV_QPT_UC:
        case IBV_QPT_RC:
            endpoint->SetQpn(ntohl(info->info.channel.qp_num));
            break;
        default:
            LOG(ERROR) << "Currently we don't support other type of QP";
    }
}

void rdma_context::GetEndpointInfo(rdma_endpoint *endpoint, struct connect_info *info) {
    memset(info, 0, sizeof(connect_info));
    info->type = htonl(kChannelInfoKey);
    switch (endpoint->GetType()) {
        case IBV_QPT_UD:
            info->info.channel.dlid = htonl(lid_);
            info->info.channel.sl = htonl(sl_); // Fall through
        case IBV_QPT_UC:
        case IBV_QPT_RC:
            info->info.channel.qp_num = htonl(endpoint->GetQpn());
            break;
        default:
            LOG(ERROR) << "Currently we don't support other type of QP";
    }
}

rdma_buffer *rdma_context::CreateBufferFromInfo(struct connect_info *info) {
    uint64_t remote_addr = ntohll(info->info.memory.remote_addr);
    uint32_t rkey = ntohl(info->info.memory.rkey);
    int size = ntohl(info->info.memory.size);
    return new rdma_buffer(remote_addr, size, 0, rkey);
}

void rdma_context::SetInfoByBuffer(struct connect_info *info, rdma_buffer *buf) {
    info->type = htonl(kMemInfoKey);
    info->info.memory.size = htonl(buf->size_);
    info->info.memory.rkey = htonl(buf->rkey_);
    info->info.memory.remote_addr = htonll(buf->addr_);
    return;
}

int rdma_context::AcceptHandler(int connfd) {
    int n, number_of_qp, number_of_mem, left, right;
    char *conn_buf = (char *)malloc(sizeof(connect_info));
    connect_info *info = (connect_info *)conn_buf;
    union ibv_gid gid;
    std::vector<rdma_buffer *> buffers;
    auto reqs = ParseRecvFromStr();
    int rbuf_id = -1;
    if (!conn_buf) {
        LOG(ERROR) << "Malloc for exchange buffer failed";
        return -1;
    }
    n = read(connfd, conn_buf, sizeof(connect_info));
    if (n != sizeof(connect_info)) {
        PLOG(ERROR) << "Server Read";
        LOG(ERROR) << n << "/" << (int)sizeof(connect_info) << ": Couldn't read remote address";
        goto out;
    }
    if (ntohl(info->type) != kHostInfoKey) {
        LOG(ERROR) << "The first exchange type should be " << kHostInfoKey;
        goto out;
    }
    number_of_qp = ntohl(info->info.host.number_of_qp);
    number_of_mem = ntohl(info->info.host.number_of_mem);
    if (number_of_qp <= 0) {
        LOG(ERROR) << "The number of qp should be positive";
        goto out;
    }
    numlock_.lock();
    if (num_of_recv_ + number_of_qp > num_per_host_ * num_of_hosts_) {
        LOG(ERROR) << "QP Overflow, request rejected";
        numlock_.unlock();
        memset(info, 0, sizeof(connect_info));
        if (write(connfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info))
            PLOG(ERROR) << "Write Error";
        goto out;
    }
    left = num_of_recv_;
    num_of_recv_ += number_of_qp;
    numlock_.unlock();
    right = left + number_of_qp;
    // Copy the remote gid.
    memcpy(&gid, &info->info.host.gid, sizeof(union ibv_gid));

    // Put local info to connect_info and send
    memset(info, 0, sizeof(connect_info));
    info->type = htonl(kHostInfoKey);
    memcpy(&info->info.host.gid, &local_gid_, sizeof(union ibv_gid));
    info->info.host.number_of_qp = htonl(number_of_qp);
    if (write(connfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
        LOG(ERROR) << "Couldn't send local address";
        goto out;
    }

    // Get the memory info from remote
    for (int i = 0; i < number_of_mem; i++) {
        n = read(connfd, conn_buf, sizeof(connect_info));
        if (n != sizeof(connect_info)) {
            PLOG(ERROR) << "Server read";
            LOG(ERROR) << n << "/" << (int)sizeof(connect_info) << ": Read " << i << " mem's info failed";
            goto out;
        }
        if (ntohl(info->type) != kMemInfoKey) {
            LOG(ERROR) << "Exchange MemInfo failed. Type received is " << ntohl(info->type);
            goto out;
        }
        auto remote_buf = CreateBufferFromInfo(info);
        buffers.push_back(remote_buf);
        auto buf = PickNextBuffer(1);
        if (!buf) {
            LOG(ERROR) << "Server using buffer error";
            goto out;
        }
        SetInfoByBuffer(info, buf);
        if (write(connfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
            LOG(ERROR) << "Couldn't send " << i << " memory's info";
            goto out;
        }
    }

    rmem_lock_.lock();
    remote_mempools_.push_back(buffers);
    rbuf_id = remote_mempools_.size() - 1;
    rmem_lock_.unlock();

    // Get the connection channel info from remote
    
    for (int i = left; i < right; i++) {
        auto ep = (rdma_endpoint *)endpoints_[i];
        n = read(connfd, conn_buf, sizeof(connect_info));
        if (n != sizeof(connect_info)) {
            PLOG(ERROR) << "Server read";
            LOG(ERROR) << n << "/" << (int)sizeof(connect_info) << ": Read " << i << " endpoint's info failed";
            goto out;
        }
        if (ntohl(info->type) != kChannelInfoKey) {
            LOG(ERROR) << "Exchange data failed. Type Error: " << ntohl(info->type);
            goto out;
        }
        SetEndpointInfo(ep, info);
        GetEndpointInfo(ep, info);
        if (write(connfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
            LOG(ERROR) << "Couldn't send " << i << " endpoint's info";
            goto out;
        }
        if (ep->Activate(gid)) {
            LOG(ERROR) << "Activate Recv Endpoint " << i << " failed";
            goto out;
        }
        // Post The first batch
        int first_batch = FLAGS_recv_wq_depth;
        int batch_size = FLAGS_recv_batch;
        size_t idx = 0;
        if (FLAGS_recv_batch > 0)
            while (ep->GetRecvCredits() > 0) {
                auto num_to_post = std::min(first_batch, batch_size);
                for (auto& req : reqs) {
                    for (int i = 0; i < req.sge_num; i++) {
                        auto buf = PickNextBuffer(1);
                        req.sglist[i].addr = buf->addr_;
                        req.sglist[i].lkey = buf->lkey_;
                    }
                }
                if (ep->PostRecv(reqs, idx, num_to_post)) {
                    LOG(ERROR) << "The " << i << " Receiver Post first batch error";
                    goto out;
                }
                first_batch -= num_to_post;
            }
        ep->SetActivated(true);
        ep->SetMemId(rbuf_id);
        ep->SetServer(GidToIP(gid));
        LOG(INFO) << "Endpoint " << i << " has started";
    }

    // After connection setup. Tell remote that they can send.
    n = read(connfd, conn_buf, sizeof(connect_info));
    if (n != sizeof(connect_info)) {
        PLOG(ERROR) << "Server read";
        LOG(ERROR) << n << "/" << (int)sizeof(connect_info)
                   << ": Read Send request failed";
        goto out;
    }
    if (ntohl(info->type) != kGoGoKey) {
        LOG(ERROR) << "GOGO request failed";
        goto out;
    }
    memset(info, 0, sizeof(connect_info));
    info->type = htonl(kGoGoKey);
    if (write(connfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
        LOG(ERROR) << "Couldn't send GOGO!!";
        goto out;
    }
    close(connfd);
    free(conn_buf);
    return 0;
out:
    close(connfd);
    free(conn_buf);
    return -1;
}

int rdma_context::ConnectionSetup(const char *server, int port) {
    struct addrinfo *res, *t;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char *service;
    int n;
    int sockfd = -1;
    if (asprintf(&service, "%d", port) < 0)
        return -1;
    n = getaddrinfo(server, service, &hints, &res);
    if (n < 0) {
        LOG(ERROR) << gai_strerror(n) << " for " << server << ":" << port;
        free(service);
        return -1;
    }
    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
                break;
            close(sockfd);
            sockfd = -1;
        }
    }
    freeaddrinfo(res);
    free(service);
    if (sockfd < 0) {
        LOG(ERROR) << "Couldn't connect to " << server << ":" << port;
        return -1;
    }
    return sockfd;
}

int rdma_context::Connect(const char *server, int port, int connid) {
    int sockfd = -1;
    for (int i = 0; i < kMaxConnRetry; i++) {
        sockfd = ConnectionSetup(server, port);
        if (sockfd > 0)
            break;
        LOG(INFO) << "Try connect to " << server << ":" << port << " failed for " << i + 1 << " times...";
        sleep(1);
    }
    if (sockfd < 0)
        return -1;
    rdma_endpoint *ep;
    union ibv_gid remote_gid;
    char *conn_buf = (char *)malloc(sizeof(connect_info));
    if (!conn_buf) {
        LOG(ERROR) << "Malloc for metadata failed";
        return -1;
    }
    connect_info *info = (connect_info *)conn_buf;
    int number_of_qp, n = 0, rbuf_id = -1;
    std::vector<rdma_buffer *> buffers;
    memset(info, 0, sizeof(connect_info));
    info->info.host.number_of_qp = htonl(num_per_host_);
    info->info.host.number_of_mem = htonl(FLAGS_buf_num);
    memcpy(&info->info.host.gid, &local_gid_, sizeof(union ibv_gid));
    if (write(sockfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
        LOG(ERROR) << "Couldn't send local address";
        n = -1;
        goto out;
    }
    n = read(sockfd, conn_buf, sizeof(connect_info));
    if (n != sizeof(connect_info)) {
        PLOG(ERROR) << "client read";
        LOG(ERROR) << "Read only " << n << "/" << sizeof(connect_info) << " bytes";
        goto out;
    }
    if (info->type != kHostInfoKey) {
        LOG(ERROR) << "The First exchange should be host info";
        goto out;
    }
    number_of_qp = htonl(info->info.host.number_of_qp);
    if (number_of_qp != num_per_host_) {
        LOG(ERROR) << "Receiver does not support " << num_per_host_ << " senders";
        goto out;
    }
    memcpy(&remote_gid, &info->info.host.gid, sizeof(union ibv_gid));
    for (int i = 0; i < FLAGS_buf_num; i++) {
        auto buf = PickNextBuffer(1);
        if (!buf) {
            LOG(ERROR) << "Client using buffer error";
            goto out;
        }
        SetInfoByBuffer(info, buf);
        if (write(sockfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
            LOG(ERROR) << "Couldn't send " << i << " memory's info";
            goto out;
        }
        n = read(sockfd, conn_buf, sizeof(connect_info));
        if (n != sizeof(connect_info)) {
            PLOG(ERROR) << "Client read";
            LOG(ERROR) << n << "/" << (int)sizeof(connect_info) << ": Read " << i << " mem's info failed";
            goto out;
        }
        if (ntohl(info->type) != kMemInfoKey) {
            LOG(ERROR) << "Exchange MemInfo failde. Type received is " << ntohl(info->type);
            goto out;
        }
        auto remote_buf = CreateBufferFromInfo(info);
        buffers.push_back(remote_buf);
    }

    rmem_lock_.lock();
    rbuf_id = remote_mempools_.size();
    remote_mempools_.push_back(buffers);
    rmem_lock_.unlock();

    for (int i = 0; i < num_per_host_; i++) {
        ep = endpoints_[i + connid * num_per_host_];
        GetEndpointInfo(ep, info);
        if (write(sockfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
            LOG(ERROR) << "Couldn't send " << i << "endpoint's info";
            goto out;
        }
        n = read(sockfd, conn_buf, sizeof(connect_info));
        if (n != sizeof(connect_info)) {
            PLOG(ERROR) << "Client Read";
            LOG(ERROR) << "Read only " << n << "/" << sizeof(connect_info) << " bytes";
            goto out;
        }
        if (ntohl(info->type) != kChannelInfoKey) {
            LOG(ERROR) << "Exchange Data Failed. Type Received is " << ntohl(info->type) << ", expected " << kChannelInfoKey;
            goto out;
        }
        SetEndpointInfo(ep, info);
        if (ep->Activate(remote_gid)) {
            LOG(ERROR) << "Activate " << i << " endpoint failed";
            goto out;
        }
    }
    memset(info, 0, sizeof(connect_info));
    info->type = htonl(kGoGoKey);
    if (write(sockfd, conn_buf, sizeof(connect_info)) != sizeof(connect_info)) {
        LOG(ERROR) << "Ask GOGO send failed";
        goto out;
    }
    n = read(sockfd, conn_buf, sizeof(connect_info));
    if (n != sizeof(connect_info)) {
        PLOG(ERROR) << "Client Read";
        LOG(ERROR) << "Read only " << n << " / " << sizeof(connect_info) << " bytes";
        goto out;
    }
    if (ntohl(info->type) != kGoGoKey) {
        LOG(ERROR) << "Ask to Send failed. Receiver reply with " << ntohl(info->type) << " But we expect " << kGoGoKey;
        goto out;
    }
    for (int i = 0; i < num_per_host_; i++) {
        auto ep = endpoints_[i + connid * num_per_host_];
        ep->SetActivated(true);
        ep->SetServer(GidToIP(remote_gid));
        ep->SetMemId(rbuf_id);
    }
    close(sockfd);
    free(conn_buf);
    return 0;
out:
    close(sockfd);
    free(conn_buf);
    return -1;
}

int rdma_context::PollEach(struct ibv_cq *cq) {
    int n = 0, ret = 0;
    struct ibv_wc wc[kCqPollDepth];
    do {
        n = ibv_poll_cq(cq, kCqPollDepth, wc);
        if (n < 0) {
            PLOG(ERROR) << "ibv_poll_cq() failed";
            return -1;
        }
        for (int i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                LOG(ERROR) << "Got bad completion status with " << wc[i].status;
                return -1;
            }
            auto ep = reinterpret_cast<rdma_endpoint*>(wc[i].wr_id);
            switch (wc[i].opcode) {
                case IBV_WC_RDMA_WRITE:
                case IBV_WC_RDMA_READ:
                case IBV_WC_SEND:
                case IBV_WC_COMP_SWAP:
                case IBV_WC_FETCH_ADD:    
                    // Client Handle CQE
                    // For Temp test
                    nic_process_time_.push_back(Now64()-(ep->start_time_));
                    ep->SendHandler(&wc[i]);
                    break;
                case IBV_WC_RECV:
                case IBV_WC_RECV_RDMA_WITH_IMM:
                    // Server Handle CQE
                    ep->RecvHandler(&wc[i]);
                    break;
                default:
                    LOG(ERROR) << "Unknown opcode " << wc[i].opcode;
                    return -1;
            }
        }
        ret += n;
    } while (n);
    return ret;
}

int rdma_context::ServerDatapath() {
    int batch_size = FLAGS_recv_batch;
    auto reqs = ParseRecvFromStr();
    size_t idx = 0;
    while (true) {
        // Replenesh Recv Buffer
        if (FLAGS_recv_batch > 0)
            for (auto ep : endpoints_) {
                if (!ep || !ep->GetActivated() || ep->GetRecvCredits() <= 0)
                    continue;
                auto credits = ep->GetRecvCredits();
                while (credits > 0) {
                    auto toPostRecv = std::min(credits, batch_size);
                    for (auto& req : reqs) {
                        for (int i = 0; i < req.sge_num; i++) {
                            auto buf = PickNextBuffer(1);
                            req.sglist[i].addr = buf->addr_;
                            req.sglist[i].lkey = buf->lkey_;
                        }
                    }
                    if (ep->PostRecv(reqs, idx, toPostRecv)) {
                        LOG(ERROR) << "PostRecv() failed";
                        break;
                    }
                    credits -= toPostRecv;
                }
            }
        // Poll out the possible completion
        for (auto cq : recv_cqs_) {
            if (PollEach(cq.cq) < 0) {
                LOG(ERROR) << "PollEach() failed";
                while (1);
                exit(0);
            }
        }
    }
    // Never reach here
    return 0;
}

int rdma_context::MeasureThp() {
    polling_thread_ = std::thread(&SetShmThread);
    polling_thread_.detach();
    return ClientDatapath();
}


int rdma_context::ClientDatapath() {
    auto req_vec = ParseReqFromStr();
    uint32_t batch_size = FLAGS_send_batch;
    uint64_t last_ts = 0;
    size_t j = 0;
    int iterations_left = FLAGS_iters;
    bool run_infinitely = FLAGS_run_infinitely;
    while (true) {
        if (!run_infinitely && iterations_left <= 0)
            break;
        for (auto ep : endpoints_) {
            if (!ep) continue;                                // Ignore those dead ones
            if (!ep->GetActivated()) continue;                // YOU ARE NOT PREPARED!
            if ( (int)batch_size > ep->GetSendCredits()) continue;  // YOU DON'T HAVE MONEY!
            // Shuffle the buffer that is used.
            for (auto& req : req_vec) {
                for (int i = 0; i < req.sge_num; i++) {
                    auto buf = PickNextBuffer(0);
                    req.sglist[i].addr = buf->addr_;
                    req.sglist[i].lkey = buf->lkey_;
                }
            }
            ep->PostSend(req_vec, j, batch_size, remote_mempools_[ep->GetMemId()]);
        }
        for (auto cq : send_cqs_) {
            auto ret = PollEach(cq.cq);
            if (ret < 0) {
                LOG(ERROR) << "PollEach() for Sender failed";
                exit(1);
            }
            if (ret) iterations_left--; // Send at least iterations msgs.
        }
        for (auto cq : recv_cqs_) {
            if (PollEach(cq.cq) < 0) {
                LOG(ERROR) << "PollEach() for Receiver failed";
                exit(1);
            }
        }
        auto ts = Now64();
        if (_print_thp) {
            if (ts-last_ts > 1000000) {
                double sum_bw = 0.0, sum_thp = 0.0;
                for (auto ep : endpoints_) {
                    if (!ep) continue;                  // Ignore those dead ones
                    if (!ep->GetActivated()) continue;  // YOU ARE NOT PREPARED!
                    //ep->PrintThroughput(ts);
                    double bw, thp;
                    ep->GetThroughput(&bw, &thp, ts);
                    sum_bw += bw, sum_thp += thp;
                }
                LOG(INFO) << "(Gbps,Mrps) is " << sum_bw << "," << sum_thp;
                // std::cout << sum_bw << "," << sum_thp << std::endl;
                last_ts = ts;
            }
        }
       // if (canExit) return 0;
    }
    for (auto lat : nic_process_time_) {
        LOG(INFO) << lat;
    }
    // Never reach here.
    return 0;
}

}  // namespace Collie
