// MIT License

// Copyright (c) 2022 Duke University. All rights reserved.
// Copyright (c) 2021 ByteDance Inc. All rights reserved.

// See LICENSE for license information

#include "rdma_memory.hpp"

#include <malloc.h>

namespace Collie {

int rdma_region::Mallocate(bool flag=false) {
    auto buf_size = num_ * size_;
    int mrflags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
    char *buffer = nullptr;
#ifdef USE_CUDA
    if (FLAGS_use_cuda) {
        CUdeviceptr d_A;
        int error;
        const size_t gpu_page_size = 64 * 1024;
        size_t size = (buf_size + gpu_page_size - 1) &
                      ~(gpu_page_size - 1);

        printf("cuMemAlloc() of a %zd bytes GPU buffer\n",
               buf_size);
        error = cuMemAlloc(&d_A, size);
        if (error != CUDA_SUCCESS) {
            printf("cuMemAlloc error=%d\n", error);
            return -1;
        }

        printf("allocated GPU buffer address at %016llx pointer=%p\n",
               d_A, (void *)d_A);
        buffer = (char *)d_A;
    } else {
        // TODO[kongxinhao]: numa-aware memory allocation
        if (align_) {
            buffer = (char *)memalign(sysconf(_SC_PAGESIZE), buf_size);
        } else {
            buffer = (char *)malloc(buf_size);
        }
    }
#else
    if (align_) {
        buffer = (char *)memalign(sysconf(_SC_PAGESIZE), buf_size);
    } else {
        buffer = (char *)malloc(buf_size);
    }
#endif
    if (!buffer) {
        PLOG(ERROR) << "Memory Allocation Failed";
        return -1;
    }
    if (FLAGS_odp) {
        mrflags |= IBV_ACCESS_ON_DEMAND;
        mr_ = ibv_reg_mr(pd_, nullptr, uint64_t(-1), mrflags);
        if (FLAGS_prefetch && flag) {
            struct ibv_sge sg_list;
            int ret;
            sg_list.lkey = mr_->lkey;
            sg_list.addr = (uint64_t)buffer;
            sg_list.length = buf_size;
            auto before = Now64();
            ret = ibv_advise_mr(pd_,  IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE,  IB_UVERBS_ADVISE_MR_FLAG_FLUSH, &sg_list, 1);
            auto end = Now64();
            if (ret) {
                PLOG(ERROR) << "ibv_advise_mr() failed";
                exit(1);
            }
            LOG(INFO) << "Advise: " << buf_size << " consumes " << end-before << " us";
        }
    } else mr_ = ibv_reg_mr(pd_, buffer, buf_size, mrflags);
//    mr_ = ibv_reg_mr(pd_, buffer, buf_size, mrflags);
    if (!mr_) {
        LOG(ERROR) << "ibv_reg_mr() failed";
        return -1;
    }
    for (int i = 0; i < num_; i++) {
        rdma_buffer *rbuf = new rdma_buffer((uint64_t)(buffer + size_ * i), size_, mr_->lkey, mr_->rkey);
        buffers_.push_back(rbuf);
    }
    return 0;
}

rdma_buffer *rdma_region::GetBuffer() {
    if (buffers_.empty()) {
        LOG(ERROR) << "The MR's buffer is empty";
        return nullptr;
    }
    auto rbuf = buffers_[ret_idx_++];
    if (ret_idx_ == buffers_.size()) ret_idx_ = 0;
    return rbuf;
}

};  // namespace Collie