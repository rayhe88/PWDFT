#ifndef _GDEVICE_HPP_
#define _GDEVICE_HPP_

#ifdef NWPW_SYCL
/* can place sycl mkl code here */
#include        <cstdio>
#include        <iostream>
#include        <limits>
#include        <CL/sycl.hpp>
#include        "mkl_blas_sycl.hpp"


class Gdevice {
   int npack,ne;

   oneapi::mkl::transpose matT = oneapi::mkl::transpose::trans;
   oneapi::mkl::transpose matN = oneapi::mkl::transpose::nontrans;

    auto asyncHandler = [&](cl::sycl::exception_list eL) {
       for (auto& e : eL) {
         try {
           std::rethrow_exception(e);
         } catch (cl::sycl::exception& e) {
           std::cout << e.what() << std::endl;
           std::cout << "fail" << std::endl;
           std::terminate();
         }
       }
    };
    cl::sycl::gpu_selector device_selector;
    cl::sycl::queue device_queue(device_selector,
                                 asyncHandler,
                                 cl::sycl::property_list{cl::sycl::property::queue::in_order{}});
    /* device memory */
    double *dev_a,*dev_b,*dev_c;

public:

    Gdevice(const int npack0, const int ne0) {
       npack = npack0;
       ne    = ne0;
       /* Creating 1D buffers for mkl matrices */
       double* dev_a = cl::sycl::malloc_device<double>(npack*ne, device_queue);
       double* dev_b = cl::sycl::malloc_device<double>(npack*ne, device_queue);
       double* dev_c = cl::sycl::malloc_device<double>(ne*ne,    device_queue);
    }

    ~Gdevice() {
       cl::sycl::free(dev_a, device_queue);
       cl::sycl::free(dev_b, device_queue);
       cl::sycl::free(dev_c, device_queue);
     }

     void ffm_dgemm(double alpha, const double *host_a, const double *host_b, double beta, double *host_c) {
        try {
           device_queue.submit([&](cl::sycl::handler& cgh) { cgh.memcpy(dev_a, host_a, npack*ne*sizeof(double)); });
           device_queue.submit([&](cl::sycl::handler& cgh) { cgh.memcpy(dev_b, host_b, npack*ne*sizeof(double)); });

           oneapi::mkl::blas::gemm(device_queue, matT, matN, ne,ne,npack, alpha, dev_a, npack, dev_b, npack, beta, dev_c, ne);
           device_queue.memcpy(host_c, dev_c, ne*ne*sizeof(double));
           device_queue.wait();
        }
        catch(cl::sycl::exception const& e) {
           std::cout << "\t\tSYCL exception during GEMM\n" << e.what() << std::endl << "OpenCL status: " << e.get_cl_code() << std::endl;
        }
     }

     void fmf_dgemm(double alpha, const double *host_a, const double *host_c, double beta, double *host_b) {
        try {
           device_queue.submit([&](cl::sycl::handler& cgh) { cgh.memcpy(dev_a, host_a, npack*ne*sizeof(double)); });
           device_queue.submit([&](cl::sycl::handler& cgh) { cgh.memcpy(dev_c, host_c, ne*ne*sizeof(double)); });

           oneapi::mkl::blas::gemm(device_queue, matN, matN, npack,ne,ne, alpha, dev_a, npack, dev_c, ne, beta, dev_b, npack);
           device_queue.memcpy(host_b, dev_b, npack*ne*sizeof(double));
           device_queue.wait();
        }
        catch(cl::sycl::exception const& e) {
           std::cout << "\t\tSYCL exception during GEMM\n" << e.what() << std::endl << "OpenCL status: " << e.get_cl_code() << std::endl;
        }
     }
};


#elif NWPW_OPENCL
/* can place opencl code from mac here */
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_SILENCE_DEPRECATION
#ifdef __APPLE__
#include <OpenCL/opencl.h>

#else
#include <CL/cl.h>
#endif


#define progam1	"#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n\n \
__kernel void NNmatmul(const int M, const int N, const int K,
                     const __global double *A, \n \
                     const __global double *B, \n \
                     __global double *C) {\n\n \
    
    // Get the index of the current element
    int i = get_global_id(0);\n \
    int j = get_global_id(1);\n\n \

    // Do the operation
    double acc = 0.0; \n \
    for (int l=0; l<K; l++) { \n \
       acc += A[i + l*M]*B[l + j*K]; \n \
    } \n \
    C[i+j*M] = acc; \n \
}"

#define progam2	"#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n\n \
__kernel void TNmatmul(const int M, const int N, const int K,
                     const __global double *A, \n \
                     const __global double *B, \n \
                     __global double *C) {\n\n \
    
    // Get the index of the current element
    int i = get_global_id(0);\n \
    int j = get_global_id(1);\n\n \

    // Do the operation
    double acc = 0.0; \n \
    for (int l=0; l<K; l++) { \n \
       acc += A[l + i*M]*B[l + j*K]; \n \
    } \n \
    C[i+j*M] = acc; \n \
}"


#else
/* standard host code */

#include        "blas.h"

class Gdevice {
   int npack,ne;

public:

     Gdevice(const int npack0, const int ne0) {
       npack = npack0;
       ne    = ne0;
     }

     void ffm_dgemm(double alpha, const double *host_a, const double *host_b, double beta, double *host_c) {
        DGEMM_PWDFT((char *) "T",(char *) "N",npack,ne,ne,
                    alpha,
                    host_a,npack,
                    host_b,npack,
                    beta,
                    host_c,ne);
     }

     void fmf_dgemm(double alpha, const double *host_a, const double *host_c, double beta, double *host_b) {
        DGEMM_PWDFT((char *) "N",(char *) "N",npack,ne,ne,
                    alpha,
                    host_a,npack,
                    host_c,ne,
                    beta,
                    host_b,npack);
     }

};

#endif
#endif
