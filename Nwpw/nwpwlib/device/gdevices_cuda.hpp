// NWPW_CUDA Routines

#pragma once

#include        "blas.h"

//#include        "gdevice.hpp"

#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cufft.h>

#include <stdexcept>
#include <string>
#include <sstream>
#include <iostream>

class cuda_exception : public std::exception {

    std::string file_;
    int         line_;
    cudaError_t err_code_;

    const char* what() const noexcept override {
        std::stringstream ss;
        ss << "CUDA Exception, " << cudaGetErrorString( err_code_ ) << " at " << std::endl
           << file_ << " : " << line_ << std::endl;
        auto msg = ss.str();
        return strdup( msg.c_str() );
    }

public:

    cuda_exception( std::string file, int line, cudaError_t err ) :
        file_(file), line_(line), err_code_(err) { }
};

class cufft_exception : public std::exception {

    std::string file_;
    int         line_;
    cufftResult err_code_;

    static const char* _cudaGetErrorEnum(cufftResult error) {
        switch (error) {
        case CUFFT_SUCCESS:
            return "CUFFT_SUCCESS";

        case CUFFT_INVALID_PLAN:
            return "CUFFT_INVALID_PLAN";

        case CUFFT_ALLOC_FAILED:
            return "CUFFT_ALLOC_FAILED";

        case CUFFT_INVALID_TYPE:
            return "CUFFT_INVALID_TYPE";

        case CUFFT_INVALID_VALUE:
            return "CUFFT_INVALID_VALUE";

        case CUFFT_INTERNAL_ERROR:
            return "CUFFT_INTERNAL_ERROR";

        case CUFFT_EXEC_FAILED:
            return "CUFFT_EXEC_FAILED";

        case CUFFT_SETUP_FAILED:
            return "CUFFT_SETUP_FAILED";

        case CUFFT_INVALID_SIZE:
            return "CUFFT_INVALID_SIZE";

        case CUFFT_UNALIGNED_DATA:
            return "CUFFT_UNALIGNED_DATA";
        }
        return "<unknown>";
    }

    const char* what() const noexcept override {
        std::stringstream ss;
        ss << "CUFFT Exception, " << " Error Code: " << _cudaGetErrorEnum(err_code_) << std::endl
           << " at " << file_ << " : " << line_ << std::endl;
        auto msg = ss.str();
        return strdup( msg.c_str() );
    }

public:

    cufft_exception( std::string file, int line, cufftResult err ) :
        file_(file), line_(line), err_code_(err) { }
};

class cublas_exception : public std::exception {

    std::string file_;
    int         line_;
    cublasStatus_t err_code_;

    static const char* _cudaGetErrorEnum(cublasStatus_t error) {
        switch (error)
        {
        case CUBLAS_STATUS_SUCCESS:
            return "CUBLAS_STATUS_SUCCESS";

        case CUBLAS_STATUS_NOT_INITIALIZED:
            return "CUBLAS_STATUS_NOT_INITIALIZED";

        case CUBLAS_STATUS_ALLOC_FAILED:
            return "CUBLAS_STATUS_ALLOC_FAILED";

        case CUBLAS_STATUS_INVALID_VALUE:
            return "CUBLAS_STATUS_INVALID_VALUE";

        case CUBLAS_STATUS_ARCH_MISMATCH:
            return "CUBLAS_STATUS_ARCH_MISMATCH";

        case CUBLAS_STATUS_MAPPING_ERROR:
            return "CUBLAS_STATUS_MAPPING_ERROR";

        case CUBLAS_STATUS_EXECUTION_FAILED:
            return "CUBLAS_STATUS_EXECUTION_FAILED";

        case CUBLAS_STATUS_INTERNAL_ERROR:
            return "CUBLAS_STATUS_INTERNAL_ERROR";
        }

        return "<unknown>";
    }

    const char* what() const noexcept override {
        std::stringstream ss;
        ss << "CUBLAS Exception, " << " Error Code: " << _cudaGetErrorEnum(err_code_) << std::endl
           << " at " << file_ << " : " << line_ << std::endl;
        auto msg = ss.str();
        return strdup( msg.c_str() );
    }

public:

    cublas_exception( std::string file, int line, cublasStatus_t err ) :
        file_(file), line_(line), err_code_(err) { }
};

#define NWPW_CUDA_ERROR( ERR )                                  \
    if( ERR != cudaSuccess )                                    \
        throw cuda_exception( __FILE__, __LINE__, ERR );

#define NWPW_CUFFT_ERROR( ERR )                                 \
    if( ERR != CUFFT_SUCCESS )                                  \
        throw cufft_exception( __FILE__, __LINE__, ERR );

#define NWPW_CUBLAS_ERROR( ERR )                                \
    if( ERR != CUBLAS_STATUS_SUCCESS )                          \
        throw cublas_exception( __FILE__, __LINE__, ERR );




class Gdevices {

    cufftHandle forward_plan_x = 0 , plan_y = 0, plan_z = 0;
    cufftHandle backward_plan_x = 0;

    cublasHandle_t master_handle = 0;
    cublasOperation_t matT = CUBLAS_OP_T;
    cublasOperation_t matN = CUBLAS_OP_N;

    cudaStream_t stream[2];

public:
    bool hasgpu = true;

    /* device memory */
    int    ndev_mem = 0;
    bool   inuse[25] = {false};
    size_t ndsize_mem[25];
    double *dev_mem[25];
    int    tile_fac=1;
    int    tile_npack2_max;
    int    tile_npack2[9],tile_start2[9];;
    double *a_psi,*a_hpsi,*b_prj;
    int    ia_psi[2],ia_hpsi[2],ib_prj[2];

    /* constructor */
    Gdevices() {
        ndev_mem = 0;

       std::cout << "Into cublasCreate" << std::endl;

        NWPW_CUBLAS_ERROR( cublasCreate(&master_handle) );

        // allocate cuda streams
        for (auto i=0; i<2; ++i) cudaStreamCreate(&stream[i]);
    }

    /* deconstructor */
    ~Gdevices() {
        // free cuda streams
        for (auto i=0; i<2; ++i) cudaStreamDestroy(stream[i]);

        cublasDestroy(master_handle);

        // free fft descriptors
        cufftDestroy(forward_plan_x);
        cufftDestroy(plan_y);
        cufftDestroy(plan_z);
        cufftDestroy(backward_plan_x);
    }

    int fetch_dev_mem_indx(const size_t ndsize) {
        int ii = 0;
        while ((((ndsize!=ndsize_mem[ii]) || inuse[ii])) && (ii<ndev_mem))
            ++ii;

        if (ii<ndev_mem) {
            inuse[ii] = true;
        } else {
            ii            = ndev_mem;
            inuse[ii]     = true;
            ndsize_mem[ii] = ndsize;
            NWPW_CUDA_ERROR( cudaMalloc((void**)&(dev_mem[ii]), ndsize*sizeof(double)) );
            ndev_mem += 1;
        }

	NWPW_CUDA_ERROR( cudaMemset(dev_mem[ii], 0, ndsize*sizeof(double)) );
        return ii;
    }

    void TN3_dgemm(int npack2, int ne, double alpha, double *host_a, double *host_b, double beta, double *host_caa, double *host_cab, double *host_cbb)
        {
            int ic11 = fetch_dev_mem_indx(((size_t) ne)    * ((size_t) ne));
            int ic12 = fetch_dev_mem_indx(((size_t) ne)    * ((size_t) ne));
            int ic22 = fetch_dev_mem_indx(((size_t) ne)    * ((size_t) ne));


            if (std::fabs(beta)>0.0) {
               NWPW_CUDA_ERROR( cudaMemcpy(dev_mem[ic11], host_caa,ne*ne*sizeof(double),cudaMemcpyHostToDevice) );
               NWPW_CUDA_ERROR( cudaMemcpy(dev_mem[ic12], host_cab,ne*ne*sizeof(double),cudaMemcpyHostToDevice) );
               NWPW_CUDA_ERROR( cudaMemcpy(dev_mem[ic22], host_cbb,ne*ne*sizeof(double),cudaMemcpyHostToDevice) );
            }

            //NWPW_CUDA_ERROR( cudaMemcpy(dev_mem[ia_psi[0]], host_a,npack*ne*sizeof(double),cudaMemcpyHostToDevice) );
            //NWPW_CUDA_ERROR( cudaMemcpy(dev_mem[ia_hpsi[0]],host_b,npack*ne*sizeof(double),cudaMemcpyHostToDevice) );
            NWPW_CUBLAS_ERROR( cublasSetMatrixAsync(tile_npack2[0],ne,sizeof(double),
                                               &host_a[tile_start2[0]],npack2,
                                               dev_mem[ia_psi[0]],tile_npack2[0],stream[0]) );
            NWPW_CUBLAS_ERROR( cublasSetMatrixAsync(tile_npack2[0],ne,sizeof(double),
                                               &host_b[tile_start2[0]],npack2,
                                               dev_mem[ia_hpsi[0]],tile_npack2[0],stream[0]) );

            double beta0 = beta;
            for (auto tt=0; tt<tile_fac; ++tt) {
               int ttp1 = tt+1;
               if (ttp1<tile_fac) {
                  NWPW_CUBLAS_ERROR( cublasSetMatrixAsync(tile_npack2[ttp1],ne,sizeof(double),
                                                     &host_a[tile_start2[ttp1]],npack2,
                                                     dev_mem[ia_psi[ttp1%2]],tile_npack2[ttp1],stream[ttp1]) );
                  NWPW_CUBLAS_ERROR( cublasSetMatrixAsync(tile_npack2[ttp1],ne,sizeof(double),
                                                     &host_b[tile_start2[ttp1]],npack2,
                                                     dev_mem[ia_hpsi[ttp1%2]],tile_npack2[ttp1],stream[ttp1]) );
               }
               NWPW_CUDA_ERROR( cudaStreamSynchronize(stream[tt]) );
               NWPW_CUBLAS_ERROR( cublasDgemm(master_handle,
                                              matT, matN,
                                              ne,ne,tile_npack2[tt],&alpha,
                                              dev_mem[ia_psi[tt%2]], tile_npack2[tt],
                                              dev_mem[ia_psi[tt%2]], tile_npack2[tt],
                                              &beta0,dev_mem[ic11],ne) );
               NWPW_CUBLAS_ERROR( cublasDgemm(master_handle,
                                              matT, matN,
                                              ne,ne,tile_npack2[tt],&alpha,
                                              dev_mem[ia_psi[tt%2]], tile_npack2[tt],
                                              dev_mem[ia_hpsi[tt%2]],tile_npack2[tt],
                                              &beta0,dev_mem[ic12],ne) );
               NWPW_CUBLAS_ERROR( cublasDgemm(master_handle,
                                              matT, matN,
                                              ne,ne,tile_npack2[tt],&alpha,
                                              dev_mem[ia_hpsi[tt%2]],tile_npack2[tt],
                                              dev_mem[ia_hpsi[tt%2]],tile_npack2[tt],
                                              &beta0,dev_mem[ic22],ne) );
               beta0 = 0.0;
            }

            NWPW_CUDA_ERROR( cudaMemcpy(host_caa,dev_mem[ic11],ne*ne*sizeof(double),cudaMemcpyDeviceToHost) );
            NWPW_CUDA_ERROR( cudaMemcpy(host_cab,dev_mem[ic12],ne*ne*sizeof(double),cudaMemcpyDeviceToHost) );
            NWPW_CUDA_ERROR( cudaMemcpy(host_cbb,dev_mem[ic22],ne*ne*sizeof(double),cudaMemcpyDeviceToHost) );

            inuse[ic11] = false;
            inuse[ic12] = false;
            inuse[ic22] = false;

        }

    void NN_dgemm(int npack2, int ne, double alpha, double *host_a, double *host_b, double beta, double *host_c) {

     DGEMM_PWDFT((char *) "N",(char *) "N",npack2,ne,ne,alpha,host_a,npack2,host_b,ne,beta,host_c,npack2);
/*
        int ib = fetch_dev_mem_indx(((size_t) ne)    * ((size_t) ne));

        NWPW_CUDA_ERROR(   cudaMemcpy(dev_mem[ib],host_b,ne*ne*sizeof(double),cudaMemcpyHostToDevice) );
        NWPW_CUBLAS_ERROR( cublasDgemm(master_handle,
				       matN,matN,
				       npack2,ne,ne,&alpha,
				       dev_mem[ia_psi[0]],npack2,
				       dev_mem[ib],ne,
				       &beta,dev_mem[ia_hpsi[0]],npack2) );

        NWPW_CUDA_ERROR( cudaMemcpy(host_c,dev_mem[ia_hpsi[0]],npack2*ne*sizeof(double),cudaMemcpyDeviceToHost) );

        inuse[ib] = false;
   */
    }


    void TN_dgemm(int ne, int nprj, int npack2, double alpha, double *host_a, double *host_b, double beta, double *host_c) {
        //gdevice_TN_dgemm(nn,nprj,ng,rtwo,a,b,rzero,sum);

        //int ia = fetch_dev_mem_indx(((size_t) npack2) * ((size_t) ne));
        //int ib = fetch_dev_mem_indx(((size_t) npack2) * ((size_t) nprj));
        b_prj  = host_b;
        ib_prj[0] = fetch_dev_mem_indx(((size_t) tile_npack2_max) * ((size_t) nprj));
        int ic = fetch_dev_mem_indx(((size_t) ne)         * ((size_t) nprj));

        //cudaMemcpy(dev_mem[ia],host_a,npack2*ne*sizeof(double));
        //cudaMemcpy(dev_mem[ib],host_b,npack2*nprj*sizeof(double));

           //NWPW_CUDA_ERROR(   cudaMemcpy(dev_mem[ib_prj[0]],host_b,npack2*nprj*sizeof(double),cudaMemcpyHostToDevice) );
           //NWPW_CUBLAS_ERROR( cublasDgemm(master_handle,
           //				       matT,matN,
           //				       ne,nprj,npack2,&alpha,
           //				       dev_mem[ia_psi[0]],npack2,
           //				       dev_mem[ib_prj[0]],   npack2,
	   //			       &beta,dev_mem[ic], ne) );

        double beta0 = beta;
        for (auto tt=0; tt<tile_fac; ++tt) {
           NWPW_CUBLAS_ERROR( cublasSetMatrix(tile_npack2[tt],ne,sizeof(double),
                              &a_psi[tile_start2[tt]],npack2,
                              dev_mem[ia_psi[tt%2]],tile_npack2[tt]) );
           NWPW_CUBLAS_ERROR( cublasSetMatrix(tile_npack2[tt],nprj,sizeof(double),
                              &b_prj[tile_start2[tt]],npack2,
                              dev_mem[ib_prj[tt%2]],tile_npack2[tt]) );
           NWPW_CUBLAS_ERROR( cublasDgemm(master_handle,
				       matT,matN,
				       ne,nprj,tile_npack2[tt],&alpha,
				       dev_mem[ia_psi[tt%2]],tile_npack2[tt],
				       dev_mem[ib_prj[tt%2]],tile_npack2[tt],
				       &beta0,dev_mem[ic],ne) );
           beta0 = 0.0;
        }
      
        cudaMemcpy(host_c,dev_mem[ic],ne*nprj*sizeof(double),cudaMemcpyDeviceToHost);

        //inuse[ia] = false;
        //inuse[ib_prj[0]] = false;
        inuse[ic] = false;
    }

    void T_free() { inuse[ib_prj[0]] = false; }

    void NT_dgemm(int npack2, int ne, int nprj, double alpha, double *host_a, double *host_b, double beta, double *host_c) {

        int ib = fetch_dev_mem_indx(((size_t) ne)    * ((size_t) nprj));
        NWPW_CUDA_ERROR(   cudaMemcpy(dev_mem[ib],host_b,ne*nprj*sizeof(double),cudaMemcpyHostToDevice) );

        for (auto tt=0; tt<tile_fac; ++tt) {
           NWPW_CUBLAS_ERROR( cublasSetMatrix(tile_npack2[tt],ne,sizeof(double),
                              &a_hpsi[tile_start2[tt]],npack2,
                              dev_mem[ia_hpsi[tt%2]],tile_npack2[tt]) );
           NWPW_CUBLAS_ERROR( cublasSetMatrix(tile_npack2[tt],nprj,sizeof(double),
                              &b_prj[tile_start2[tt]],npack2,
                              dev_mem[ib_prj[tt%2]],tile_npack2[tt]) );
           NWPW_CUBLAS_ERROR( cublasDgemm(master_handle,
 				          matN,matT,
				          tile_npack2[tt],ne,nprj, &alpha,
				          dev_mem[ib_prj[tt%2]],tile_npack2[tt],
				          dev_mem[ib],ne,
				          &beta,dev_mem[ia_hpsi[tt%2]],tile_npack2[tt]) );
           NWPW_CUBLAS_ERROR( cublasGetMatrix(tile_npack2[tt],ne,sizeof(double),
                                              dev_mem[ia_hpsi[tt]],tile_npack2[tt],
                                              &a_hpsi[tile_start2[tt]],npack2) );
        }

        inuse[ib] = false;
        inuse[ib_prj[0]] = false;
    }


    /********************/
    /* psi_dev functions*/
    /********************/
    void psi_alloc(int npack1, int ne, int tile_fac0=1) {
        tile_fac        = tile_fac0;
        tile_npack2_max = (((2*npack1)%tile_fac)==0) ? (2*npack1)/tile_fac : (2*npack1)/tile_fac + 1;
        for (auto i=0; i<tile_fac; ++i) tile_npack2[i] = (((2*npack1)%tile_fac)<i) ? (2*npack1)/tile_fac+1 : (2*npack1)/tile_fac;

        tile_start2[0] = 0;
        for (auto i=1; i<tile_fac; ++i) tile_start2[i] = tile_start2[i-1] + tile_npack2[i-1];

        ia_psi[0]  = fetch_dev_mem_indx(((size_t) tile_npack2_max) * ((size_t) ne));
        ia_hpsi[0] = fetch_dev_mem_indx(((size_t) tile_npack2_max) * ((size_t) ne));

        if (tile_fac>1) {
           ia_psi[1]  = fetch_dev_mem_indx(((size_t) tile_npack2_max) * ((size_t) ne));
           ia_hpsi[1] = fetch_dev_mem_indx(((size_t) tile_npack2_max) * ((size_t) ne));
        }
    }
    void psi_dealloc() {
        inuse[ia_psi[0]]  = false;
        inuse[ia_hpsi[0]] = false;
        if (tile_fac>1) {
           inuse[ia_psi[1]]  = false;
           inuse[ia_hpsi[1]] = false;
        }
    }
    void psi_copy_host2gpu(int npack1, int ne, double *psi) {
        a_psi = psi;
        //cudaMemcpy(dev_mem[ia_psi[0]],psi,tile_npack2_max*ne*sizeof(double),cudaMemcpyHostToDevice);
        cublasSetMatrix(tile_npack2[0],ne,sizeof(double),
                        psi,2*npack1,
                        dev_mem[ia_psi[0]],tile_npack2[0]);
    }
    void hpsi_copy_host2gpu(int npack1, int ne, double *hpsi) {
        a_hpsi = hpsi;
        //cudaMemcpy(dev_mem[ia_hpsi[0]],hpsi,2*npack1*ne*sizeof(double),cudaMemcpyHostToDevice);
        cublasSetMatrix(tile_npack2[0],ne,sizeof(double),
                        hpsi,2*npack1,
                        dev_mem[ia_hpsi[0]],tile_npack2[0]);
    }
    void psi_copy_gpu2host(int npack1, int ne, double *psi) {
        //cudaMemcpy(psi, dev_mem[ia_psi[0]], 2*ne*npack1*sizeof(double),cudaMemcpyDeviceToHost);
        if (tile_fac==1)
           cublasGetMatrix(tile_npack2[0],ne,sizeof(double),
                           dev_mem[ia_psi[0]],tile_npack2[0],
                           psi,2*npack1);
    }
    void hpsi_copy_gpu2host(int npack1, int ne, double *hpsi) {
        //cudaMemcpy(hpsi, dev_mem[ia_hpsi[0]], 2*ne*npack1*sizeof(double),cudaMemcpyDeviceToHost);
        if (tile_fac==1)
           cublasGetMatrix(tile_npack2[0],ne,sizeof(double),
                           dev_mem[ia_hpsi[0]],tile_npack2[0],
                           hpsi,2*npack1);
    }


    /******************************/
    /* fft functions (uses cuFFT) */
    /******************************/
    void batch_fft_init(int nx, int ny, int nz, int nq1, int nq2, int nq3) {
    std::cout << "Into batch_fft_init" << std::endl;
        NWPW_CUFFT_ERROR( cufftPlan1d(&forward_plan_x, nx, CUFFT_D2Z, nq1) );
        NWPW_CUFFT_ERROR( cufftPlan1d(&backward_plan_x, nx, CUFFT_Z2D, nq1) );

        int y_inembed[] = {ny};
        int y_onembed[] = {ny};
        NWPW_CUFFT_ERROR( cufftPlanMany(&plan_y, 1, &ny, y_inembed, 1, ny, y_onembed, 1, ny, CUFFT_Z2Z, nq2) );

        int z_inembed[] = {nz};
        int z_onembed[] = {nz};
        NWPW_CUFFT_ERROR( cufftPlanMany(&plan_z, 1, &nz, z_inembed, 1, nz, z_onembed, 1, nz, CUFFT_Z2Z, nq3) );
    }

    void batch_cfftx(bool forward, int nx, int nq, int n2ft3d, double *a) {
        int ia_dev = fetch_dev_mem_indx(((size_t) n2ft3d));
        cudaMemcpy(dev_mem[ia_dev],a,n2ft3d*sizeof(double),cudaMemcpyHostToDevice);

        if (forward) {
            NWPW_CUFFT_ERROR( cufftExecD2Z(forward_plan_x,
                                           reinterpret_cast<cufftDoubleReal*>(dev_mem[ia_dev]),
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev])) );
        }
        else {
            NWPW_CUFFT_ERROR( cufftExecZ2D(backward_plan_x,
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           reinterpret_cast<cufftDoubleReal*>(dev_mem[ia_dev])) );
        }

        cudaMemcpy(a,dev_mem[ia_dev],n2ft3d*sizeof(double),cudaMemcpyDeviceToHost);

        inuse[ia_dev] = false;
    }

    void batch_cffty(bool forward, int ny,int nq,int n2ft3d, double *a) {
        int ia_dev = fetch_dev_mem_indx(((size_t) n2ft3d));
        cudaMemcpy(dev_mem[ia_dev],a,n2ft3d*sizeof(double),cudaMemcpyHostToDevice);

        if (forward) {
            NWPW_CUFFT_ERROR( cufftExecZ2Z(plan_y,
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           CUFFT_FORWARD) );
        }
        else {
            NWPW_CUFFT_ERROR( cufftExecZ2Z(plan_y,
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           CUFFT_INVERSE) );
        }

        cudaMemcpy(a,dev_mem[ia_dev],n2ft3d*sizeof(double),cudaMemcpyDeviceToHost);

        inuse[ia_dev] = false;
    }

    void batch_cfftz(bool forward, int nz,int nq,int n2ft3d, double *a) {
        int ia_dev = fetch_dev_mem_indx(((size_t) n2ft3d));
        cudaMemcpy(dev_mem[ia_dev],a,n2ft3d*sizeof(double),cudaMemcpyHostToDevice);

        if (forward) {
            NWPW_CUFFT_ERROR( cufftExecZ2Z(plan_z,
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           CUFFT_FORWARD) );
        }
        else {
            NWPW_CUFFT_ERROR( cufftExecZ2Z(plan_z,
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           reinterpret_cast<cufftDoubleComplex*>(dev_mem[ia_dev]),
                                           CUFFT_INVERSE) );
        }

        cudaMemcpy(a,dev_mem[ia_dev],n2ft3d*sizeof(double),cudaMemcpyDeviceToHost);

        inuse[ia_dev] = false;
    }

};
