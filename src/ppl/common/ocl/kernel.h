// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef _ST_HPC_PPL_COMMON_OCL_KERNEL_H_
#define _ST_HPC_PPL_COMMON_OCL_KERNEL_H_

#include "framechain.h"
#include "kernelpool.h"

#include <string>
#include <vector>

#include "ppl/common/log.h"

namespace ppl { namespace common { namespace ocl {

#define SET_PROGRAM_SOURCE(frame_chain) frame_chain->setSource(source_string);

bool getKernelNames(const cl_program program,
                    std::vector<std::string>& kernel_names);
bool compileOclKernels(FrameChain* frame_chain);
bool validateNDrange(cl_uint work_dims, size_t* global_work_size,
                     size_t* local_work_size);
bool enqueueOclKernel(FrameChain* frame_chain, const char* kernel_name,
                      const cl_kernel& kernel, cl_uint work_dims,
                      const size_t* global_work_size,
                      const size_t* local_work_size);

template <size_t INDEX, typename T>
cl_int setKernelArg(const cl_kernel& kernel, const T& value) {
    cl_int error_code;
    error_code = clSetKernelArg(kernel, INDEX, sizeof(T), (void*)&value);
    if (error_code != CL_SUCCESS) {
        LOG(ERROR) << "Call clSetKernelArg() with the argument index " << INDEX
                   << " failed with code: "  << error_code;
    }

    return error_code;
}

template <size_t INDEX, typename T, typename... Args>
cl_int setKernelArg(const cl_kernel& kernel, const T& value,
                    const Args&... rest) {
    cl_int error_code;
    error_code = clSetKernelArg(kernel, INDEX, sizeof(T), &value);
    if (error_code != CL_SUCCESS) {
        LOG(ERROR) << "Call clSetKernelArg() with the argument index " << INDEX
                   << " failed with code: "  << error_code;
        return error_code;
    }

    return setKernelArg<INDEX + 1, Args...>(kernel, rest...);
}

template <typename... Args>
void runOclKernel(FrameChain* frame_chain, const char* kernel_name_cstr,
                  cl_uint work_dims, size_t* global_work_size,
                  size_t* local_work_size, Args... args) {
    cl_int error_code;
    bool succeeded;
    cl_context context = frame_chain->getContext();
    std::string project_name = frame_chain->getProjectName();
    std::string kernel_name = kernel_name_cstr;
    cl_kernel kernel = getKernelFromPool(context, project_name, kernel_name);
    if (kernel == nullptr) {
        succeeded = compileOclKernels(frame_chain);
        if (!succeeded) {
            LOG(ERROR) << "Failed to compile " << kernel_name;
            return;
        }
        cl_program program = frame_chain->getProgram();

        std::vector<std::string> kernel_names;
        getKernelNames(program, kernel_names);
        cl_kernel temp_kernel = nullptr;
        for (size_t i = 0; i < kernel_names.size(); i++) {
            temp_kernel = clCreateKernel(program, kernel_names[i].c_str(),
                                         &error_code);
            if (error_code != CL_SUCCESS) {
                LOG(ERROR) << "Call clCreateKernel() failed with code: "
                           << error_code;
                return;
            }
            if (kernel_names[i] == kernel_name) {
                kernel = temp_kernel;
            }

            succeeded = insertKernelToPool(context, project_name,
                                           kernel_names[i], temp_kernel);
            if (!succeeded) {
                LOG(ERROR) << "Failed to insert kernel " << kernel_names[i]
                           << " to kernel pool.";
            }
        }
    }
    if (kernel == nullptr) {
        LOG(ERROR) << "No valid kernel to run.";
        return;
    }

    error_code = setKernelArg<0, Args...>(kernel, args...);
    if (error_code != CL_SUCCESS) {
        return;
    }

    succeeded = validateNDrange(work_dims, global_work_size, local_work_size);
    if (!succeeded) {
        LOG(ERROR) << "Invalid NDrange of kernel " << kernel_name_cstr;
        return;
    }

    succeeded = enqueueOclKernel(frame_chain, kernel_name_cstr, kernel,
                                 work_dims, global_work_size, local_work_size);
    if (!succeeded) {
        LOG(ERROR) << "Failed to enqueue kernel " << kernel_name_cstr;
        return;
    }
}

}}}

#endif
