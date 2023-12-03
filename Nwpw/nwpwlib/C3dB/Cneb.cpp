/* Cneb.cpp
   Author - Eric Bylaska

        this class is used for defining 3d parallel maps
*/

/**
 * @brief Class representing the Cneb (Parallel NEB) calculation.
 *
 * The `Cneb` class is responsible for managing the parallel Complex NEB (Cneb) calculation.
 * It inherits properties and methods from several base classes such as `CGrid` and `c1db`.
 * The Cneb calculation involves complex operations related to parallelization,
 * matrix manipulations, and more.
 */


#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring> //memset()
#include <iostream>
#include <sstream>
#include <stdexcept> // runtime_error()

#include "Cneb.hpp"

#include "blas.h"

namespace pwdft {


/********************************
 *                              *
 *         Constructors         *
 *                              *
 ********************************/
// Cneb::Cneb(Parallel *inparall, Lattice *inlattice, Control2& control, int
// ispin, int *ne, int nbrill)
//   : CGrid(inparall, inlattice, control),
//     c1db(inparall,control.mapping1d(),ispin,ne)

Cneb::Cneb(Parallel *inparall, Lattice *inlattice, Control2 &control, int ispin, int *ne, Brillouin *inbrill)
     : CGrid(inparall, inlattice, control, inbrill),
       c1db(inparall, control.mapping1d(), ispin, ne) 
{
   int np_i = c1db::parall->np_i();
   int np_j = c1db::parall->np_j();
   int np_k = c1db::parall->np_k();
   int taskid_i = c1db::parall->taskid_i();
   int taskid_j = c1db::parall->taskid_j();
   int taskid_k = c1db::parall->taskid_k();

   parallelized = (np_j > 1);
 
   s22 = new (std::nothrow) double[7 * ne[0] * ne[0]]();
   s21 = &s22[1 * ne[0] * ne[0]];
   s12 = &s22[2 * ne[0] * ne[0]];
   s11 = &s22[3 * ne[0] * ne[0]];
   sa1 = &s22[4 * ne[0] * ne[0]];
   sa0 = &s22[5 * ne[0] * ne[0]];
   st1 = &s22[6 * ne[0] * ne[0]];
 
   if (parallelized) 
   {
      mcq[0] = mcq[1] = 0;
      ncq[0] = ncq[1] = 0;
      for (auto ms=0; ms<ispin; ++ms) 
      {
         ma[ms]  = new (std::nothrow) int[np_i];
         ma1[ms] = new (std::nothrow) int[np_i];
         ma2[ms] = new (std::nothrow) int[np_i];
         mc[ms] = new (std::nothrow) int[np_i];
         na[ms] = new (std::nothrow) int[np_j];
         nc[ms] = new (std::nothrow) int[np_j];
         std::memset(ma[ms], 0,np_i*sizeof(int));
         std::memset(ma1[ms],0,np_i*sizeof(int));
         std::memset(ma2[ms],0,np_i*sizeof(int));
         std::memset(mc[ms], 0,np_i*sizeof(int));

         std::memset(na[ms],0,np_j*sizeof(int));
         std::memset(nc[ms],0,np_j*sizeof(int));
 
         {  auto i=0; auto j=0;
            for (auto k=0; k<ne[ms]; ++k)
            {
               mc[ms][i] += 1;
               nc[ms][j] += 1;
               na[ms][j] += 1;
               i = (i+1)%np_i;
               j = (j+1)%np_j;
            }
         }
 
         ma[ms][taskid_i]  = 2*CGrid::npack1_max();
         //ma1[ms][taskid_i] = 2*CGrid::nzero(1);
         ma2[ms][taskid_i] = n2ft3d;
         c1db::parall->Vector_ISumAll(1,np_i,ma[ms]);
         c1db::parall->Vector_ISumAll(1,np_i,ma1[ms]);
         c1db::parall->Vector_ISumAll(1,np_i,ma2[ms]);
 
         mcq[ms] = mc[ms][taskid_i];
         ncq[ms] = nc[ms][taskid_j];
 
         mcqmax[ms] = 0;
         for (auto i=0; i<np_i; ++i)
             if (mc[ms][i] > mcqmax[ms])
                mcqmax[ms] = mc[ms][i];
 
         ncqmax[ms] = 0;
         for (auto j=0; j<np_j; ++j)
             if (nc[ms][j] > ncqmax[ms])
                ncqmax[ms] = nc[ms][j];
              
         ncqmax0 = 0;
         for (auto j=0; j<np_j; ++j)
            if (nc[0][j] > ncqmax0)
               ncqmax0 = nc[0][j];
 
         npack1_all = 0;
         nida1_all  = 0;
         n2ft3d_all = 0;
         for (auto i=0; i<np_i; ++i)
         {
            npack1_all += ma[ms][i];
            nida1_all  += ma1[ms][i];
            n2ft3d_all += ma2[ms][i];
         }
      }


      work1 = new (std::nothrow) double[2*2*64*ma2[0][taskid_i]]();
      int nework = 2*64*nc[0][taskid_j];
      if (nework<mcq[0]*ncq[0]) nework = mcq[0]*ncq[0];
      work2 = new (std::nothrow) double[3*nework]();
      bcolwork = new (std::nothrow) double[ne[0]*ncqmax0]();
      bwork2   = new (std::nothrow) double[ne[0]*ncqmax0]();
      //rwork1   = new (std::nothrow) double[2*(na[0][0]*ma2[0][taskid_i])]();
      //rwork2   = new (std::nothrow) double[2*(na[0][0]*ma2[0][taskid_i])]();
      rwork1   = new (std::nothrow) double[2*(na[0][0]*ma2[0][0])]();
      rwork2   = new (std::nothrow) double[2*(na[0][0]*ma2[0][0])]();
      mat_tmp  = new (std::nothrow) double[3*(mcq[0]*ncq[0] + mcq[1]*ncq[1])]();

      mindx[0] = new (std::nothrow) int[(mcq[0]*ncq[0] + mcq[1]*ncq[1])]();
      mindx[1] = new (std::nothrow) int[(mcq[0]*ncq[0])]();
      if (ispin>1) mindx[2] = new (std::nothrow) int[(mcq[1]*ncq[1])]();

      mall[0]  = ne[0]*ne[0] + ne[1]*ne[1];
      mall[1]  = ne[0]*ne[0];
      mall[2]  = ne[1]*ne[1];
      mpack[0] = mcq[0]*ncq[0] + mcq[1]*ncq[1];
      mpack[1] = mcq[0]*ncq[0];
      mpack[2] = mcq[1]*ncq[1];

      int indx0=0;
      int indx1=0;
      int indx2=0;
      int jj=0;
      int jcur=0;
      for (auto j=0; j<ne[0]; ++j)
      {
         auto ii=0;
         auto icur=0;
         for (auto i=0; i<ne[0]; ++i)
         {
            if ((icur==taskid_i) && (jcur==taskid_j))
            {
               mindx[0][indx0] = i + j*ne[0];
               mindx[1][indx1] = i + j*ne[0];
               ++indx0;
               ++indx1;
            }
            ++ii;
            if (ii>=mc[0][icur])
            {
               ++icur;
               ii=0;
            }
         }
         ++jj;
         if (jj>=nc[0][jcur])
         {
            ++jcur;
            jj=0;
         }
      }
      if (ispin>1)
      {
         int jj=0;
         int jcur=0;
         for (auto j=0; j<ne[1]; ++j)
         {
            auto ii=0;
            auto icur=0;
            for (auto i=0; i<ne[1]; ++i)
            {
               if ((icur==taskid_i) && (jcur==taskid_j))
               {
                  mindx[0][indx0] = i + j*ne[1] + ne[0]*ne[0];
                  mindx[2][indx2] = i + j*ne[1];
                  ++indx0;
                  ++indx2;
               }
               ++ii;
               if (ii>=mc[1][icur])
               {
                  ++icur;
                  ii=0;
               }
            }
            ++jj;
            if (jj>=nc[1][jcur])
            {
               ++jcur;
               jj=0;
            }
         }
      }
   }
 
   g_rnd_algorithm = control.initial_psi_random_algorithm();

   io_norbs_max = control.io_norbs_max();
   io_buffer    = control.io_buffer();
}

/*************************************
 *                                   *
 *      Cneb::g_generate1_random     *
 *                                   *
 *************************************/
void Cneb::g_generate1_random(double *psi) 
{
   double *tmp2 = new (std::nothrow) double[n2ft3d]();

   int filling[4], nfft[3];
   double zvalue[2];

   int ibshiftj = 2*CGrid::npack1_max();
   int ibshiftk = ibshiftj*(neq[0]+neq[1]);
 
   nfft[0] = nx;
   nfft[1] = ny;
   nfft[2] = nz;
 
   int taskid_k = c1db::parall->taskid_k();
   int taskid_j = c1db::parall->taskid_j();
   for (auto nb=0; nb<nbrillouin; ++nb)
   {
      int qk = ktoindex(nb);
      int pk = ktop(nb);
      int nbq1 = qk+1;
      for (auto ms=0; ms<ispin; ++ms)
      for (auto n=0; n<ne[ms]; ++n) 
      {
         util_getfilling(n, nfft, filling, zvalue);
        
         int qj = msntoindex(ms, n);
         int pj = msntop(ms, n);
        
         if ((pj==taskid_j) && (pk==taskid_k))
         {
            r_zero(tmp2);
            c3db::c_setpw(filling, zvalue, tmp2);
            c3db::c_addrandom(tmp2);
           
            CGrid::c_pack(nbq1, tmp2);
            int indx = ibshiftj*qj + ibshiftk*qk;
            CGrid::cc_pack_copy(nbq1, tmp2, psi + indx);
            CGrid::c_pack_noimagzero(nbq1, psi + indx);
         }
      }
   }
   delete [] tmp2;
}

void Cneb::g_generate2_random(double *psi) 
{
   double *tmp2 = new (std::nothrow) double[n2ft3d]();
   int ibshiftj = 2*CGrid::npack1_max();
   int ibshiftk = ibshiftj*(neq[0]+neq[1]);
 
   int taskid_k = c1db::parall->taskid_k();
   int taskid_j = c1db::parall->taskid_j();
   for (auto nb=0; nb<nbrillouin; ++nb)
   {
      int qk = ktoindex(nb);
      int pk = ktop(nb);
      int nbq1 = qk+1;
      for (auto ms=0; ms<ispin; ++ms)
      for (auto n=0; n<ne[ms]; ++n) 
         {
            int qj = msntoindex(ms, n);
            int pj = msntop(ms, n);
            if ((pj == taskid_j) && (pk==taskid_k))
            {
              c3db::r_setrandom(tmp2);
              c3db::rc_fft3d(tmp2);
           
              CGrid::c_pack(nbq1, tmp2);
              int indx = ibshiftj*qj + ibshiftk*qk;
              CGrid::cc_pack_copy(nbq1, tmp2, psi + indx);
            }
      }
   }
   delete[] tmp2;
}

/*************************************
 *                                   *
 *      Cneb::g_generate_random      *
 *                                   *
 *************************************/
void Cneb::g_generate_random(double *psi) {
  int taskid = c1db::parall->taskid();
  util_random(taskid + 91);

  if (g_rnd_algorithm == 1)
    this->g_generate1_random(psi);
  else
    this->g_generate2_random(psi);
}

/*************************************
 *                                   *
 *           Cneb::g_read            *
 *                                   *
 *************************************/
 /**
 * @brief Read data from an input unit within a parallel computing framework.
 *
 * This function is responsible for reading data from a specified input unit based
 * on the input parameters. It operates within a parallel computing framework and uses
 * the CGrid class for data manipulation.
 *
 * @param iunit An integer specifying the input unit to read data from.
 * @param psi A pointer to a double array where the read data will be stored.
 *
 * @return None.
 */
void Cneb::g_read(const int iunit, double *psi) 
{
   double *tmp2 = new (std::nothrow) double[n2ft3d]();
   int ibshiftj = 2*CGrid::npack1_max();
   int ibshiftk = ibshiftj*(neq[0]+neq[1]);
 
   int taskid_k = c1db::parall->taskid_k();
   int taskid_j = c1db::parall->taskid_j();

   for (auto nb=0; nb<nbrillouin; ++nb)
   {
      int qk = ktoindex(nb);
      int pk = ktop(nb);
      int nbq1 = qk+1;
      for (auto ms=0; ms<ispin; ++ms)
      for (auto n=0; n<ne[ms]; ++n) 
      {
         int qj = msntoindex(ms, n);
         int pj = msntop(ms, n);
         c_read(iunit, tmp2, pj, -1);
         if ((pj==taskid_j) && (pk==taskid_k))
         {
            int indx = ibshiftj*qj + ibshiftk*qk;
            CGrid::c_pack(nbq1, tmp2);
            CGrid::cc_pack_copy(nbq1, tmp2, psi + indx);
         }
      }
   }
   delete[] tmp2;
}


/*************************************
 *                                   *
 *           Cneb::g_read_ne         *
 *                                   *
 *************************************/
void Cneb::g_read_ne(const int iunit, const int *ne0, double *psi) 
{
   double *tmp2 = new (std::nothrow) double[n2ft3d]();
   int ibshiftj = 2*CGrid::npack1_max();
   int ibshiftk = ibshiftj*(neq[0]+neq[1]);
 
   int taskid_k = c1db::parall->taskid_k();
   int taskid_j = c1db::parall->taskid_j();
 
   for (auto nb=0; nb<nbrillouin; ++nb)
   {
      int qk = ktoindex(nb);
      int pk = ktop(nb);
      int nbq1 = qk+1;

      for (auto ms=0; ms<ispin; ++ms)
      for (auto n=0; n<ne[ms]; ++n) 
      {
         int qj = msntoindex(ms, n);
         int pj = msntop(ms, n);

         if (n<ne0[ms])
         {
            c3db::c_read(iunit, tmp2, pj,-1);
         }
         else
         {
            c3db::r_setrandom(tmp2);
            c3db::rc_fft3d(tmp2);
         }

         if ((pj==taskid_j) && (pk==taskid_k)) 
         {
            int indx = ibshiftj*qj + ibshiftk*qk;
            CGrid::c_pack(nbq1, tmp2);
            CGrid::cc_pack_copy(nbq1, tmp2, psi + indx);
         }
      }
   }
 
   delete[] tmp2;
}



/*************************************
 *                                   *
 *           Cneb::g_write           *
 *                                   *
 *************************************/
/**
 * @brief Write data to an output unit within a parallel computing framework.
 *
 * This function is responsible for writing data to a specified output unit based on
 * the input parameters. It operates within a parallel computing framework and uses
 * the CGrid class for data manipulation.
 *
 * @param iunit An integer specifying the output unit to write data to.
 * @param psi A pointer to a double array containing the data to be written.
 *
 * @return None.
 */
void Cneb::g_write(const int iunit, double *psi) 
{
   int npack2 = 2*CGrid::npack1_max();
   int ibshiftj = npack2;
   int ibshiftk = ibshiftj*(neq[0]+neq[1]);
   double *tmp2 = new (std::nothrow) double[n2ft3d]();

   int taskid_k = c1db::parall->taskid_j();
   int taskid_j = c1db::parall->taskid_j();

   for (auto nb=0; nb<nbrillouin; ++nb)
   {
      int qk = ktoindex(nb);
      int pk = ktop(nb);
      int nbq1 = qk+1;
      for (auto ms=0; ms<ispin; ++ms)
      for (auto n=0; n<ne[ms]; ++n) 
      {
         int qj = msntoindex(ms, n);
         int pj = msntop(ms, n);
         if ((pj==taskid_j) && (pk==taskid_k))
         {
            int indx = ibshiftj*qj + ibshiftk*qk;
            CGrid::cc_pack_copy(nbq1, psi+indx, tmp2);
            CGrid::c_unpack(nbq1, tmp2);
         }
         if (io_buffer)
            c_write_buffer(iunit,tmp2,pj,pk);
         else
            c_write(iunit,tmp2,pj,pk);
      }
   }

   delete[] tmp2;
}


/*************************************
 *                                   *
 *           Cneb::h_read            *
 *                                   *
 *************************************/
void Cneb::h_read(const int iunit, const int nproj, double *proj)
{
   int taskid_k = c1db::parall->taskid_k();
   double *tmp2 = new (std::nothrow) double[n2ft3d]();

   for (auto nb=0; nb<nbrillouin; ++nb)
   for (auto n=0;  n<nproj;        ++n)
   {
      int qk = ktoindex(nb);
      int pk = ktop(nb);
      t_read(iunit, tmp2, -1, -1);
      if (pk == taskid_k)
      {
         int indx = n*n2ft3d + nproj*n2ft3d*qk;
         c3db::rr_copy(tmp2, proj+indx);
      }
   }
   delete[] tmp2;
}


/*************************************
 *                                   *
 *           Cneb::h_write           *
 *                                   *
 *************************************/
void Cneb::h_write(const int iunit, const int nproj, const double *proj) 
{
   int taskid_k = c1db::parall->taskid_k();
   double *tmp2 = new (std::nothrow) double[n2ft3d]();

   for (auto nb=0; nb<nbrillouin; ++nb)
   for (auto n=0;   n<nproj;       ++n)
   {
      int qk = ktoindex(nb);
      int pk = ktop(nb);
      if (pk == taskid_k)
      {
         int indx = n*n2ft3d + nproj*n2ft3d*qk;
         c3db::rr_copy(proj+indx, tmp2);
      }
      if (io_buffer)
         t_write_buffer(iunit,tmp2,-1, -1);
      else
         t_write(iunit,tmp2,-1, -1);
   }

   delete[] tmp2;
}


/*************************************
 *                                   *
 *           Cneb::gg_traceall       *
 *                                   *
 *************************************/
/**
 * @brief Calculate the trace of the inner product between two sets of spinor wave functions.
 *
 * This function calculates the trace of the inner product between two sets of spinor wave functions, represented by the arrays `psi1` and `psi2`. The trace is computed by summing the inner product of corresponding elements in the arrays.
 *
 * @param psi1 A pointer to the first set of spinor wave functions.
 * @param psi2 A pointer to the second set of spinor wave functions.
 *
 * @return The trace of the inner product between the two sets of spinor wave functions.
 *
 * @note This function assumes that the sizes of `psi1` and `psi2` are compatible, and it operates on elements in pairs. The `neq` and `ispin` variables are used to determine the sizes and characteristics of the wave functions.
 */
double Cneb::gg_traceall(double *psi1, double *psi2) 
{
   int npack2 = 2*CGrid::npack1_max();
   int indx = 0;
   double sum = 0.0;

   for (auto nbq=0; nbq<nbrillq; ++nbq)
   {
      double weight = pbrill_weight(nbq);
      for (auto n=0; n<(neq[0]+neq[1]); ++n) 
      {
         sum += CGrid::cc_pack_idot(nbq+1, psi1+indx, psi2+indx)*weight;
         indx += npack2;
      }
   }
   if (ispin == 1) sum *= 2.0;
 
   return c3db::parall->SumAll(0, sum);
}


/*************************************
 *                                   *
 *           Cneb::gg_copy           *
 *                                   *
 *************************************/
/**
 * @brief Copy the elements of one array to another.
 *
 * This function copies the elements from the source array `psi1` to the destination
 * array `psi2`.
 *
 * @param psi1  The source array.
 * @param psi2  The destination array where the elements are copied.
 */
void Cneb::gg_copy(double *psi1, double *psi2) 
{
   int nsize = nbrillq * 2 * (neq[0] + neq[1]) * CGrid::npack1_max();
   std::memcpy(psi2, psi1, nsize * sizeof(double));
}


/*************************************
 *                                   *
 *           Cneb::gg_SMul           *
 *                                   *
 *************************************/
/**
 * @brief Multiply an array by a scalar: psi2 = alpha * psi1.
 *
 * This function multiplies each element of the `psi1` array by the scalar `alpha`
 * and stores the result in the `psi2` array.
 *
 * @param alpha The scalar value to multiply with each element of `psi1`.
 * @param psi1  The source array.
 * @param psi2  The destination array where the result is stored.
 */
void Cneb::gg_SMul(double alpha, double *psi1, double *psi2) 
{
   int npack2 = 2*CGrid::npack1_max();
   int nsize = nbrillq*(neq[0]+neq[1])*npack2;
   for (int i = 0; i < nsize; ++i)
      psi2[i] = alpha * psi1[i];
}
// void Cneb::g_SMul1(double alpha,double *psi1)
//{
//    int nsize = 2*(neq[0]+neq[1])*CGrid::npack(1);
//    for (int i=0; i<nsize; ++i)
//       psi1[i] *= alpha;
// }

/*************************************
 *                                   *
 *           Cneb::gg_daxpy          *
 *                                   *
 *************************************/
/**
 * @brief Perform a linear combination of two arrays: psi2 += alpha * psi1.
 *
 * This function computes the linear combination `psi2 += alpha * psi1` for arrays `psi1` and `psi2`.
 *
 * @param alpha The scaling factor to apply to the `psi1` array before adding to `psi2`.
 * @param psi1  The source array.
 * @param psi2  The destination array where the result is stored.
 */
void Cneb::gg_daxpy(double alpha, double *psi1, double *psi2) 
{
   int nsize = 2 * (neq[0] + neq[1]) * CGrid::npack1_max();
   for (int i = 0; i < nsize; ++i)
      psi2[i] += alpha * psi1[i];
}


/*************************************
 *                                   *
 *           Cneb::g_Scale           *
 *                                   *
 *************************************/
/**
 * @brief Scale the elements of an array by a given factor.
 *
 * This function scales each element of the input array `psi1` by the specified factor `alpha`.
 *
 * @param alpha The scaling factor to apply to the array elements.
 * @param psi1  The array to be scaled.
 */
void Cneb::g_Scale(double alpha, double *psi1) 
{
   int nsize = 2 * (neq[0] + neq[1]) * CGrid::npack1_max();
   for (int i = 0; i < nsize; ++i)
      psi1[i] *= alpha;
}


/*************************************
 *                                   *
 *           Cneb::gg_Sum2           *
 *                                   *
 *************************************/
/**
 * @brief Element-wise addition of two arrays.
 *
 * This function performs element-wise addition, adding the elements of
 * array `psi1` to the elements of array `psi2`, and stores the result in `psi2`.
 *
 * @param psi1  The array to add to `psi2`.
 * @param psi2  The array to which `psi1` is added, and where the result is stored.
 */
void Cneb::gg_Sum2(double *psi1, double *psi2) 
{
   int nsize = 2 * (neq[0] + neq[1]) * CGrid::npack1_max();
   for (int i = 0; i < nsize; ++i)
      psi2[i] += psi1[i];
}


/*************************************
 *                                   *
 *         Cneb::gg_Minus2           *
 *                                   *
 *************************************/
/**
 * @brief Element-wise subtraction of one array from another.
 *
 * This function performs element-wise subtraction, subtracting the elements of
 * array `psi1` from the elements of array `psi2`, and stores the result in `psi2`.
 *
 * @param psi1  The array to subtract from `psi2`.
 * @param psi2  The array from which `psi1` is subtracted, and where the result is stored.
 */
void Cneb::gg_Minus2(double *psi1, double *psi2) 
{
   int nsize = 2 * (neq[0] + neq[1]) * CGrid::npack1_max();
   for (int i = 0; i < nsize; ++i)
      psi2[i] -= psi1[i];
}


/*************************************
 *                                   *
 *         Cneb::ggg_Minus           *
 *                                   *
 *************************************/
/**
 * @brief Element-wise subtraction of two sets of spinor wave functions.
 *
 * This function performs element-wise subtraction of two sets of spinor wave functions, `psi1` and `psi2`, and stores the result in `psi3`.
 *
 * @param psi1 A pointer to the first set of spinor wave functions.
 * @param psi2 A pointer to the second set of spinor wave functions.
 * @param psi3 A pointer to the resulting set of spinor wave functions after subtraction.
 *
 * @note This function assumes that the sizes of `psi1`, `psi2`, and `psi3` are compatible, and it operates on elements in pairs. The `neq` variable is used to determine the sizes of the wave functions.
 */
void Cneb::ggg_Minus(double *psi1, double *psi2, double *psi3) 
{
   int nsize = 2 * (neq[0] + neq[1]) * CGrid::npack1_max();
   for (int i = 0; i < nsize; ++i)
      psi3[i] = psi1[i] - psi2[i];
}


/*************************************
 *                                   *
 *         Cneb::g_zero              *
 *                                   *
 *************************************/
void Cneb::g_zero(double *psi2) 
{
   int one = 1;
   int zero = 0;
   int nsize = 2 * (neq[0] + neq[1]) * CGrid::npack1_max();
   double rzero = 0.0;

   // dcopy_(&nsize,&rzero,&zero,psi2,&one);
   std::memset(psi2, 0, nsize * sizeof(double));
}


/*************************************
 *                                   *
 *         Cneb::gh_fftb             *
 *                                   *
 *************************************/
/**
 * @brief Perform a forward Fast Fourier Transform (FFT) of a set of wave functions using the PFFT3B library.
 *
 * This function performs a forward Fast Fourier Transform (FFT) of a set of wave functions, `psi`, using the PFFT3B library and stores the result in `psi_r`. The transform is performed along one dimension.

 * @param psi A pointer to the input set of wave functions to be transformed.
 * @param psi_r A pointer to the resulting transformed wave functions.
 *
 * @note This function uses the cr_pfft3b_queue library to perform the FFT. It queues the input wave functions for transformation and retrieves the transformed results.

 * @warning The behavior of this function depends on the initialization and configuration of the pfft3b_queue library. Ensure that the library is properly initialized and configured before calling this function.
 */
void Cneb::gh_fftb(double *psi, double *psi_r) 
{
   nwpw_timing_function ftimer(1);
   int n, done;
   int indx1, indx1n, shift1;
   int indx2, indx2n, shift2;
 
   n = neq[0] + neq[1];
   shift1 = 2 * CGrid::npack1_max();
   shift2 = n2ft3d;
   indx1 = indx1n = 0;
   indx2 = indx2n = 0;
   done = 0;
   while (!done) 
   {
      if (indx1 < n) 
      {
         cr_pfft3b_queuein(1, psi + indx1n);
         indx1n += shift1;
         ++indx1;
      }
      if (cr_pfft3b_queuefilled() || (indx1 >= n)) 
      {
         cr_pfft3b_queueout(1, psi_r + indx2n);
         indx2n += shift2;
         ++indx2;
      }
      done = ((indx1 >= n) && (indx2 >= n));
   }
}

/*************************************
 *                                   *
 *         Cneb::hr_aSumSqr          *
 *                                   *
 *************************************/
/**
 * @brief Calculate the sum of squares of components of a set of wave functions with an optional scaling factor.
 *
 * This function calculates the sum of squares of the components of a set of wave functions, `psir`, with an optional scaling factor `alpha`. The result is stored in the `dn` array.

 * @param alpha An optional scaling factor applied to the sum of squares.
 * @param psir A pointer to the input set of wave functions.
 * @param dn A pointer to the resulting sum of squares of components.
 *
 * @note This function performs the calculation for each component of the wave functions in the `psir` array and accumulates the results in the `dn` array.
 */
void Cneb::hr_aSumSqr(const double alpha, double *psir, double *dn) 
{
   int n,ms,k,indx0,indx1;
   int one = 1;
   int zero = 0;
   int nsize = n2ft3d * ispin;
   double rzero = 0.0;
 
   // dcopy_(&nsize,&rzero,&zero,dn,&one);
   std::memset(dn,0,nsize*sizeof(double));
 
   indx0 = 0;
   indx1 = 0;
   for (ms=0; ms<ispin; ++ms) 
   {
      for (n=0; n<(neq[ms]); ++n) 
      {
         for (k=0; k<n2ft3d; ++k)
            dn[indx0+k] += alpha*psir[indx1+k]*psir[indx1+k];
         indx1 += n2ft3d;
      }
      indx0 += n2ft3d;
   }
   c3db::parall->Vector_SumAll(2, ispin*n2ft3d, dn);
}

/*************************************
 *                                   *
 *         Cneb::hhr_aSumMul         *
 *                                   *
 *************************************/
/*
   This routine calculates the perturbation density.

   dn12(r) = Sum_i (psi0_i(r)*psi1_i(r) + psi1_i(r)*psi0_i(r) )

   where

   psi_i(r) = psi0_i(r) + lmbda*psi1_i(r)
   dn12(r) = n(r) = Sum_i (psi_i(r)*psi_i(r))
                  = Sum_i ( psi0_i(r)*psi0_i(r)
                          + lmbda*(psi0_i(r)*psi1_i(r) + psi1_i(r)*psi0_i(r))
                          + lmbda^2*psi1_i(r)*psi1_i(r) )
*/
/**
 * @brief Calculate the perturbation density with a mixed product of two sets of wave functions.
 *
 * This function calculates the perturbation density `dn12(r)` using a mixed product of two sets of wave functions `psi0_i(r)` and `psi1_i(r)` with an optional scaling factor `alpha`. The result is stored in the `dn12` array.

 * @param alpha An optional scaling factor applied to the mixed product.
 * @param psir0 A pointer to the first set of wave functions.
 * @param psir1 A pointer to the second set of wave functions.
 * @param dn12 A pointer to the resulting perturbation density.
 *
 * @note This function performs the calculation for each component of the wave functions in the `psir0` and `psir1` arrays and accumulates the results in the `dn12` array according to the formula:
 * \f[
 * dn12(r) = \sum_i \left( \psi0_i(r) \psi1_i(r) + \psi1_i(r) \psi0_i(r) \right)
 * \f]
 * where \f$\psi_i(r) = \psi0_i(r) + \lambda \psi1_i(r)\f$, and \f$\lambda\f$ is the optional scaling factor.
 */
void Cneb::hhr_aSumMul(const double alpha, const double *psir0,
                       const double *psir1, double *dn12) 
{
   int n, ms, k, indx0, indx1;
   int one = 1;
   int zero = 0;
   int nsize = n2ft3d * ispin;
   double rzero = 0.0;
 
   std::memset(dn12, 0, nsize * sizeof(double));
 
   indx0 = 0;
   indx1 = 0;
   for (ms = 0; ms < ispin; ++ms) 
   {
      for (n = 0; n < (neq[ms]); ++n) 
      {
         for (k=0; k<n2ft3d; ++k)
            dn12[indx0+k] += alpha*( psir0[indx1+k]*psir1[indx1+k] 
                                   + psir1[indx1+k]*psir0[indx1+k]);
         indx1 += n2ft3d;
      }
      indx0 += n2ft3d;
   }
   c3db::parall->Vector_SumAll(2, ispin * n2ft3d, dn12);
}

/*************************************
 *                                   *
 *      Cneb::ggm_sym_Multiply       *
 *                                   *
 *************************************/
/**
 * @brief Multiply two sets of wave functions with symmetry considerations.
 *
 * This function multiplies two sets of wave functions `psi1` and `psi2` and stores the result in the `hml` matrix while considering symmetry properties. The function is parallelized for performance improvements.

 * @param psi1 A pointer to the first set of wave functions.
 * @param psi2 A pointer to the second set of wave functions.
 * @param hml A pointer to the resulting matrix.
 *
 * @note This function performs matrix multiplication of `psi1` and `psi2` with symmetry considerations. The result is stored in the `hml` matrix, which should be pre-allocated with sufficient memory. Symmetry is taken into account to optimize the calculation.
 */
void Cneb::ggm_sym_Multiply(double *psi1, double *psi2, double *hml) 
{
   nwpw_timing_function ftimer(15);
 
   int npack1 = 2*CGrid::npack1_max();
   //int ng0    = 2*CGrid::nzero(1);
 
   int one = 1;
   double rzero = 0.0;
   double rtwo = 2.0;
   double rone = 1.0;
   double rmone = -1.0;
 
   if (parallelized) 
   {
      auto taskid_i = c1db::parall->taskid_i();
      auto taskid_j = c1db::parall->taskid_j();
      auto ishift2 = mcq[0]*ncq[0];
      for (auto ms=0; ms<ispin; ++ms) 
      {
          if (ne[ms]>0)
          {
             auto shift0 = ms*neq[0]*npack1;
             auto shift2 = ms*ishift2;
             c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                           ne[ms],ne[ms],npack1_all,128,
                           psi1+shift0,psi2+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                           mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                           work1,work2);
          }
      }
      std::memset(hml,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,hml);
      c1db::parall->Vector_SumAll(0,mall[0],hml);

      // Symmetrize hml
      for (auto ms=0; ms<ispin; ++ms)
      {
         int n = ne[ms];
         int mshift0 = ms*ne[0]*ne[0];
         for (auto k=0; k<n; ++k)
            for (auto j=k+1; j<n; ++j)
               hml[mshift0+j+k*n] = hml[mshift0+k+j*n];
      }
   } 
   else 
   {
      auto shift0  = 0;
      auto mshift0 = 0;
      for (auto ms=0; ms<ispin; ++ms) 
      {
         auto n = ne[ms];
         c3db::mygdevice.TN1_dgemm(npack1,n,rtwo,psi1+shift0,psi2+shift0,rzero,hml+mshift0);
        
         for (auto k=0; k<n; ++k)
            for (auto j=k+1; j<n; ++j)
               hml[mshift0+j+k*n] = hml[mshift0+k+j*n];
        
         shift0  += npack1*ne[0];
         mshift0 += ne[0]*ne[0];
      }
      c3db::parall->Vector_SumAll(1,ne[0]*ne[0]+ne[1]*ne[1],hml);
   }
   /*
   if (c3db::parall->is_master())
      std::cout << "hml= [" << Efmt(15,10) 
                            << hml[0] << " " << hml[4] << " " << hml[8]  << " " << hml[12]  << std::endl 
                << "      " << hml[1] << " " << hml[5] << " " << hml[9]  << " " << hml[13]  << std::endl
                << "      " << hml[2] << " " << hml[6] << " " << hml[10] << " " << hml[14]  << std::endl
                << "      " << hml[3] << " " << hml[7] << " " << hml[11] << " " << hml[15]  << "]"<< std::endl << std::endl;
                */
}


/*************************************
 *                                   *
 *        Cneb::ggm_Multiply         *
 *                                   *
 *************************************/
/**
 * @brief Multiply two sets of wave functions without considering symmetry.
 *
 * This function multiplies two sets of wave functions `psi1` and `psi2` and stores the result in the `hml` matrix without considering symmetry properties. The function is parallelized for performance improvements.

 * @param psi1 A pointer to the first set of wave functions.
 * @param psi2 A pointer to the second set of wave functions.
 * @param hml A pointer to the resulting matrix.
 *
 * @note This function performs matrix multiplication of `psi1` and `psi2` without taking symmetry into account. The result is stored in the `hml` matrix, which should be pre-allocated with sufficient memory. Parallelization is applied for improved performance.
 */
void Cneb::ggm_Multiply(double *psi1, double *psi2, double *hml) 
{
   nwpw_timing_function ftimer(15);
   int n, shift0, mshift0;
 
   int one = 1;
   int npack1 = 2 * CGrid::npack1_max();
   //int ng0 = 2 * CGrid::nzero(1);
 
   double rzero = 0.0;
   double rtwo = 2.0;
   double rone = 1.0;
   double rmone = -1.0;
 
   if (parallelized) 
   {
      //std::ostringstream msg;
      //msg << "NWPW Error: ggm_Multiply() parallelized is NOT supported\n"
      //    << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
      //throw(std::runtime_error(msg.str()));
      auto taskid_i = c1db::parall->taskid_i();
      auto taskid_j = c1db::parall->taskid_j();
      auto ishift2 = mcq[0]*ncq[0];
      for (auto ms=0; ms<ispin; ++ms)
      {  
          if (ne[ms]>0) 
          { 
             auto shift0 = ms*neq[0]*npack1;
             auto shift2 = ms*ishift2;
             c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                           ne[ms],ne[ms],npack1_all,128,
                           psi1+shift0,psi2+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                           mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                           work1,work2);
          }
      }
      std::memset(hml,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,hml);
      c1db::parall->Vector_SumAll(0,mall[0],hml);

   } 
   else 
   {
      shift0 = 0;
      mshift0 = 0;
      for (auto ms = 0; ms < ispin; ++ms) 
      {
         n = ne[ms];
         c3db::mygdevice.TN1_dgemm(npack1,n,rtwo,psi1+shift0,psi2+shift0,rzero,hml+mshift0);
        
        
         shift0 += npack1 * ne[0];
         mshift0 += ne[0] * ne[0];
      }
      c3db::parall->Vector_SumAll(1, ne[0] * ne[0] + ne[1] * ne[1], hml);
   }
}

/*************************************
 *                                   *
 *      Cneb::ffm_sym_Multiply       *
 *                                   *
 *************************************/
void Cneb::ffm_sym_Multiply(const int mb, double *psi1, double *psi2, double *hml) 
{
   nwpw_timing_function ftimer(15);
   int ms, ms1, ms2, ishift2, j, k, n, shift0, shift1, mshift0, mshift1, nn;
 
   int one = 1;
   int npack1 = 2 * CGrid::npack1_max();
   //int ng0 = 2 * CGrid::nzero(1);
 
   double rzero = 0.0;
   double rtwo = 2.0;
   double rone = 1.0;
   double rmone = -1.0;
 
   if (parallelized) 
   {
      //std::ostringstream msg;
      //msg << "NWPW Error: ffm_sym_Multiply() parallelized is NOT supported\n"
      //    << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
      //throw(std::runtime_error(msg.str()));
      if (mb==-1)
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = mcq[0]*ncq[0];
         nn = mcqmax[0]*ncqmax[0]+mcqmax[1]*ncqmax[1];
      }
      else
      {
         ms1 = mb;
         ms2 = mb+1;
         ishift2 = 0;
         nn = mcqmax[mb]*ncqmax[mb];
      }

      auto taskid_i = c1db::parall->taskid_i();
      auto taskid_j = c1db::parall->taskid_j();

      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi1+shift0,psi1+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(hml,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,hml);
      c1db::parall->Vector_SumAll(0,mall[0],hml);

      // Symmetrize hml
      for (auto ms=0; ms<ispin; ++ms)
      {
         mshift0 = ms*ne[0]*ne[0];
         n = ne[ms];
         for (auto k=0; k<n; ++k)
            for (auto j=k+1; j<n; ++j)
               hml[mshift0+j+k*n] = hml[mshift0+k+j*n];
      }

   } 
   else 
   {
      if (mb == -1) 
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = ne[0] * ne[0];
         nn = ne[0] * ne[0] + ne[1] * ne[1];
         shift0 = 0;
         mshift0 = 0;
      } 
      else 
      {
         ms1 = mb;
         ms2 = mb + 1;
         ishift2 = 0;
         nn = ne[mb] * ne[mb];
         shift0 = mb * ne[0] * npack1;
         mshift0 = 0;
      }
      for (ms = ms1; ms < ms2; ++ms) 
      {
         n = ne[ms];
         c3db::mygdevice.TN1_dgemm(npack1,n,rtwo,&psi1[shift0],&psi2[shift0],rzero,&hml[mshift0]);
        
         for (k = 0; k < n; ++k)
         for (j = k + 1; j < n; ++j)
            hml[mshift0 + j + k * n] = hml[mshift0 + k + j * n];
        
         shift0  += npack1*ne[0];
         mshift0 += ne[0]*ne[0];
      }
      c3db::parall->Vector_SumAll(1, nn, hml);
   }
}


/*************************************
 *                                   *
 *        Cneb::ffm_Multiply         *
 *                                   *
 *************************************/
void Cneb::ffm_Multiply(const int mb, double *psi1, double *psi2, double *hml) 
{
   nwpw_timing_function ftimer(15);
   int ms, ms1, ms2, ishift2, j, k, n, shift0, shift1, mshift0, mshift1, nn;
 
   int one = 1;
   //int ng = 2 * CGrid::npack(1);
   int npack1 = 2 * CGrid::npack1_max();
   //int ng0 = 2 * CGrid::nzero(1);
 
   double rzero = 0.0;
   double rtwo = 2.0;
   double rone = 1.0;
   double rmone = -1.0;
 
   if (parallelized) 
   {
      //std::ostringstream msg;
      //msg << "NWPW Error: ffm_Multiply() parallelized is NOT supported\n"
      //    << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
      //throw(std::runtime_error(msg.str()));

      if (mb==-1)
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = mcq[0]*ncq[0];
         nn = mcqmax[0]*ncqmax[0]+mcqmax[1]*ncqmax[1];
      }
      else
      {
         ms1 = mb;
         ms2 = mb+1;
         ishift2 = 0;
         nn = mcqmax[mb]*ncqmax[mb];
      }

      auto taskid_i = c1db::parall->taskid_i();
      auto taskid_j = c1db::parall->taskid_j();

      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi1+shift0,psi1+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(hml,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,hml);
      c1db::parall->Vector_SumAll(0,mall[0],hml);
         
   } 
   else 
   {
      if (mb == -1) 
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = ne[0] * ne[0];
         nn = ne[0] * ne[0] + ne[1] * ne[1];
         shift0 = 0;
         mshift0 = 0;
      } 
      else 
      {
         ms1 = mb;
         ms2 = mb + 1;
         ishift2 = 0;
         nn = ne[mb] * ne[mb];
         shift0 = mb * ne[0] * npack1;
         mshift0 = 0;
      }
      for (ms = ms1; ms < ms2; ++ms) 
      {
         n = ne[ms];
         c3db::mygdevice.TN1_dgemm(npack1,n,rtwo,&psi1[shift0],&psi2[shift0],rzero,hml+mshift0);
        
         shift0 += npack1 * ne[0];
         mshift0 += ne[0] * ne[0];
      }
      c3db::parall->Vector_SumAll(1, nn, hml);
   }
}

/*************************************
 *                                   *
 *      Cneb::ffm3_sym_Multiply      *
 *                                   *
 *************************************/

 /* 
  Description:                                    
  This function performs symmetric matrix         
  multiplication and manipulation operations. It  
  operates on input arrays psi1 and psi2, and     
  calculates and stores the results in arrays s11,
  s21, and s22. The calculations are performed based 
  on the given parameters and matrix properties.    
                                                   
  Parameters:                                     
  - mb: Index specifying the block of matrices to 
        operate on. If mb is -1, full matrix      
        multiplication is performed. Otherwise, a 
        subset of matrices is used for calculations. 
  - psi1, psi2: Pointers to double arrays containing 
                input matrix data.                  
  - s11, s21, s22: Pointers to double arrays where 
                  the results will be stored.     
*/

void Cneb::ffm3_sym_Multiply(const int mb, double *psi1, double *psi2,
                             double *s11, double *s21, double *s22) 
{
   nwpw_timing_function ftimer(15);
   int ms1, ms2, ishift2, n, shift0, shift1, mshift0, mshift1, nn;
   int one = 1;
   int npack1 = 2*CGrid::npack1_max();
   //int ng0    = 2*CGrid::nzero(1);
 
   double rzero = 0.0;
   double rtwo = 2.0;
   double rone = 1.0;
   double rmone = -1.0;
 
   if (parallelized) 
   {
      //std::ostringstream msg;
      //msg << "NWPW Error: ffm3_sym_Multiply() parallelized is NOT supported\n"
      //    << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
      //throw(std::runtime_error(msg.str()));
      if (mb==-1)
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = mcq[0]*ncq[0];
         nn = mcqmax[0]*ncqmax[0]+mcqmax[1]*ncqmax[1];
      }
      else
      {
         ms1 = mb;
         ms2 = mb+1;
         ishift2 = 0;
         nn = mcqmax[mb]*ncqmax[mb];
      }

      auto taskid_i = c1db::parall->taskid_i();
      auto taskid_j = c1db::parall->taskid_j();
      //auto ishift2 = mcq[0]*ncq[0];

      //s11
      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi1+shift0,psi1+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(s11,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,s11);
      c1db::parall->Vector_SumAll(0,mall[0],s11);

      //s21
      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi2+shift0,psi1+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(s21,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,s21);
      c1db::parall->Vector_SumAll(0,mall[0],s21);

      //s22
      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi2+shift0,psi2+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(s22,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,s22);
      c1db::parall->Vector_SumAll(0,mall[0],s22);

      // Symmetrize hml
      for (auto ms=0; ms<ispin; ++ms)
      {
         mshift0 = ms*ne[0]*ne[0];
         n = ne[ms];
         for (auto k=0; k<n; ++k)
            for (auto j=k+1; j<n; ++j)
            {
               s11[mshift0+j+k*n] = s11[mshift0+k+j*n];
               s21[mshift0+j+k*n] = s21[mshift0+k+j*n];
               s22[mshift0+j+k*n] = s22[mshift0+k+j*n];
            }
      }
   } 
   else 
   {
      if (mb == -1) 
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = ne[0]*ne[0];
         nn = ne[0]*ne[0] + ne[1]*ne[1];
         shift0 = 0;
         mshift0 = 0;
      } 
      else 
      {
         ms1 = mb;
         ms2 = mb + 1;
         ishift2 = 0;
         nn = ne[mb]*ne[mb];
         //shift0 = mb*ne[0]*ng;
         shift0 = mb*ne[0]*npack1;
         mshift0 = 0;
      }
      for (auto ms=ms1; ms<ms2; ++ms) 
      {
         n = ne[ms];
        
         c3db::mygdevice.TN3_dgemm(npack1,n,rtwo,psi1+shift0,psi2+shift0,rzero,s11+mshift0,s21+mshift0,s22+mshift0);
        
        
         for (auto k=0; k<n; ++k) 
         {
            for (auto j=k+1; j<n; ++j) 
            {
               s11[mshift0+j+k*n] = s11[mshift0+k+j*n];
               s21[mshift0+j+k*n] = s21[mshift0+k+j*n];
               s22[mshift0+j+k*n] = s22[mshift0+k+j*n];
            }
         }
        
         shift0  += npack1*ne[0];
         mshift0 += ne[0]*ne[0];
      }
      c3db::parall->Vector_SumAll(1, nn, s11);
      c3db::parall->Vector_SumAll(1, nn, s21);
      c3db::parall->Vector_SumAll(1, nn, s22);
   }
}


/*************************************
 *                                   *
 *      Cneb::ffm4_sym_Multiply      *
 *                                   *
 *************************************/
/**
 * @brief Multiply two sets of wave functions with symmetry consideration.
 *
 * This function multiplies two sets of wave functions `psi1` and `psi2` with symmetry consideration and stores the result in the `s11`, `s21`, `s12`, and `s22` matrices. The function is parallelized for performance improvements.
 *
 * @param mb An integer specifying the band index. If `mb` is -1, the function operates on all bands.
 * @param psi1 A pointer to the first set of wave functions.
 * @param psi2 A pointer to the second set of wave functions.
 * @param s11 A pointer to the resulting matrix for symmetry operation 11.
 * @param s21 A pointer to the resulting matrix for symmetry operation 21.
 * @param s12 A pointer to the resulting matrix for symmetry operation 12.
 * @param s22 A pointer to the resulting matrix for symmetry operation 22.
 *
 * @note This function performs matrix multiplication of `psi1` and `psi2` with symmetry consideration and stores the results in four different matrices `s11`, `s21`, `s12`, and `s22`. The matrices should be pre-allocated with sufficient memory. Parallelization is applied for improved performance.
 */
void Cneb::ffm4_sym_Multiply(const int mb, double *psi1, double *psi2,
                             double *s11, double *s21, double *s12, double *s22) 
{
   nwpw_timing_function ftimer(15);
   int ms, ms1, ms2, ishift2, j, k, n, shift0, shift1, mshift0, mshift1, nn;
   int one = 1;
   int npack1 = 2 * CGrid::npack1_max();
   //int ng0 = 2 * CGrid::nzero(1);
 
   double rzero = 0.0;
   double rtwo = 2.0;
   double rone = 1.0;
   double rmone = -1.0;
 
   if (parallelized) 
   {
      //std::ostringstream msg;
      //msg << "NWPW Error: ffm4_sym_Multiply() parallelized is NOT supported\n"
      //    << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
      //throw(std::runtime_error(msg.str()));
      if (mb==-1)
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = mcq[0]*ncq[0];
         nn = mcqmax[0]*ncqmax[0]+mcqmax[1]*ncqmax[1];
      }
      else
      {
         ms1 = mb;
         ms2 = mb+1;
         ishift2 = 0;
         nn = mcqmax[mb]*ncqmax[mb];
      }

      auto taskid_i = c1db::parall->taskid_i();
      auto taskid_j = c1db::parall->taskid_j();
      //auto ishift2 = mcq[0]*ncq[0];

      //s11
      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi1+shift0,psi1+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(s11,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,s11);
      c1db::parall->Vector_SumAll(0,mall[0],s11);

      //s21 
      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi2+shift0,psi1+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(s21,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,s21);
      c1db::parall->Vector_SumAll(0,mall[0],s21);

      //s12 
      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi1+shift0,psi2+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(s12,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,s12);
      c1db::parall->Vector_SumAll(0,mall[0],s12);

      //s22
      for (auto ms=0; ms<ispin; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift2 = ms*ishift2;
            c1db::CMatrix_dgemm2c(c1db::parall, &mygdevice,
                          ne[ms],ne[ms],npack1_all,128,
                          psi2+shift0,psi2+shift0, ma[ms][taskid_i],ma[ms],ma1[ms],na[ms],
                          mat_tmp+shift2,mc[ms][taskid_i],mc[ms],nc[ms],
                          work1,work2);
         }
      }
      std::memset(s22,0,mall[0]*sizeof(double));
      t_bindexcopy(mpack[0],mindx[0],mat_tmp,s22);
      c1db::parall->Vector_SumAll(0,mall[0],s22);

      // Symmetrize hml
      for (auto ms=0; ms<ispin; ++ms)
      {
         mshift0 = ms*ne[0]*ne[0];
         n = ne[ms];
         for (auto k=0; k<n; ++k)
            for (auto j=k+1; j<n; ++j)
            {
               s11[mshift0+j+k*n] = s11[mshift0+k+j*n];
               s21[mshift0+j+k*n] = s21[mshift0+k+j*n];
               s12[mshift0+j+k*n] = s12[mshift0+k+j*n];
               s22[mshift0+j+k*n] = s22[mshift0+k+j*n];
            }
      }
   } 
   else 
   {
      if (mb == -1) 
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = ne[0] * ne[0];
         nn = ne[0] * ne[0] + ne[1] * ne[1];
         shift0 = 0;
         mshift0 = 0;
      } 
      else 
      {
         ms1 = mb;
         ms2 = mb + 1;
         ishift2 = 0;
         nn = ne[mb] * ne[mb];
         shift0 = mb * ne[0] * npack1;
         mshift0 = 0;
      }
      for (ms = ms1; ms < ms2; ++ms) 
      {
         n = ne[ms];
        
         c3db::mygdevice.TN4_dgemm(npack1,n,rtwo,psi1+shift0,psi2+shift0,rzero,
                                   s11+mshift0,s21+mshift0,s12+mshift0,s22+mshift0);
        
         for (k = 0; k < n; ++k) 
         {
            for (j = k + 1; j < n; ++j) 
            {
               s11[mshift0 + j+k*n] = s11[mshift0 + k+j*n];
               s21[mshift0 + j+k*n] = s21[mshift0 + k+j*n];
               s12[mshift0 + j+k*n] = s12[mshift0 + k+j*n];
               s22[mshift0 + j+k*n] = s22[mshift0 + k+j*n];
            }
         }
        
         shift0 += npack1* ne[0];
         mshift0 += ne[0] * ne[0];
      }
      c3db::parall->Vector_SumAll(1, nn, s11);
      c3db::parall->Vector_SumAll(1, nn, s21);
      c3db::parall->Vector_SumAll(1, nn, s12);
      c3db::parall->Vector_SumAll(1, nn, s22);
   }
}

/*************************************
 *                                   *
 *        Cneb::fmf_Multiply         *
 *                                   *
 *************************************/
/**
 * @brief Multiply wave functions with a Hermitian matrix and scaling.
 *
 * This function multiplies a set of wave functions `psi1` with a Hermitian matrix `hml` and scales the result by `alpha`, then adds it to another set of wave functions `psi2` scaled by `beta`. The function is parallelized for performance improvements.
 *
 * @param mb An integer specifying the band index. If `mb` is -1, the function operates on all bands.
 * @param psi1 A pointer to the first set of wave functions.
 * @param hml A pointer to the Hermitian matrix.
 * @param alpha A scaling factor for the multiplication of `psi1` with `hml`.
 * @param psi2 A pointer to the second set of wave functions.
 * @param beta A scaling factor for the addition of the result to `psi2`.
 *
 * @note This function performs the multiplication of `psi1` with the Hermitian matrix `hml`, scales the result by `alpha`, and then adds it to `psi2` scaled by `beta`. The matrices and vectors should be pre-allocated with sufficient memory. Parallelization is applied for improved performance.
 */
void Cneb::fmf_Multiply(const int mb, double *psi1, double *hml, double alpha,
                        double *psi2, double beta) 
{
   nwpw_timing_function ftimer(16);
   int ms, ms1, ms2, n, nn,shift1, mshift1, ishift2,ishift3;
   int npack1 = 2 * CGrid::npack1_max();

/*
   if (c1db::parall->is_master())
      std::cout << "into HML= [" << Efmt(15,10)
                            << hml[0] << " " << hml[4] << " " << hml[8]  << " " << hml[12]  << std::endl
                << "      " << hml[1] << " " << hml[5] << " " << hml[9]  << " " << hml[13]  << std::endl
                << "      " << hml[2] << " " << hml[6] << " " << hml[10] << " " << hml[14]  << std::endl
                << "      " << hml[3] << " " << hml[7] << " " << hml[11] << " " << hml[15]  << "]"<< std::endl << std::endl;
                */

   if (parallelized) 
   {
      if (mb==-1)
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = mcq[0]*ncq[0];
         ishift3 = ne[0]*ne[0];
         nn = mcqmax[0]*ncqmax[0]+mcqmax[1]*ncqmax[1];
      }
      else
      {
         ms1 = mb;
         ms2 = mb+1;
         ishift2 = 0;
         ishift3 = 0;
         nn = mcqmax[mb]*ncqmax[mb];
      }
      auto taskid_i = c1db::parall->taskid_i();
      auto taskid_j = c1db::parall->taskid_j();




      for (auto ms=ms1; ms<ms2; ++ms)
      {
         if (ne[ms]>0)
         {
            auto shift0 = ms*neq[0]*npack1;
            auto shift3 = ms*ishift3;
            c1db::CMatrix_dgemm1_rot2(c1db::parall, &mygdevice,
                          npack1_all,ne[ms],ne[ms],128,
                          alpha,
                          psi1+shift0,ma[ms][taskid_i],ma[ms],na[ms],
                          hml+shift3,mcq[ms],mc[ms],nc[ms],
                          beta,
                          psi2+shift0,ma[ms][taskid_i],ma[ms],na[ms],
                          bcolwork,bwork2,rwork1,rwork2);
         }
      }
   } 
   else 
   {
      if (mb == -1) 
      {
         ms1 = 0;
         ms2 = ispin;
         ishift2 = ne[0]*ne[0];
         shift1 = 0;
         mshift1 = 0;
      } 
      else 
      {
         ms1 = mb;
         ms2 = mb + 1;
         ishift2 = 0;
         shift1 = mb * ne[0]*npack1;
         mshift1 = 0;
      }
      for (ms=ms1; ms<ms2; ++ms) 
      {
         n = ne[ms];
        
         c3db::mygdevice.NN_dgemm(npack1, n, alpha, psi1+shift1, hml+mshift1, beta, psi2+shift1);
         shift1 += ne[0]*npack1;
         mshift1 += ishift2;
      }
   }
}


/*************************************
 *                                   *
 *            Cneb::ggm_SVD          *
 *                                   *
 *************************************/
/**
 * @brief Perform Singular Value Decomposition (SVD) of a matrix.
 *
 * This function performs the Singular Value Decomposition (SVD) of a given matrix 'A'
 * and returns the components 'U', 'S', and 'V' such that A = U * S * V^T.
 *
 * @param[in] A Input matrix to be decomposed.
 * @param[out] U Output matrix 'U' with left singular vectors.
 * @param[out] S Output array 'S' containing singular values.
 * @param[out] V Output matrix 'V' with right singular vectors.
 */
void Cneb::ggm_SVD(double *A, double *U, double *S, double *V) 
{
   int n, indx;
   double *tmp2 = new (std::nothrow) double[neq[0] + neq[1]]();
 
   /* generate V and Sigma^2 */
   //if (c3db::parall->is_master()) std::cout << "generate V and Sigma2:" << std::endl;
   ggm_sym_Multiply(A, A, V);
   m_diagonalize(V, S);
   //if (c3db::parall->is_master()) 
   //    std::cout << "Sigma=" << Efmt(15,10)  << S[0] << " " << S[1] << " " << S[2] << " " << S[3] << std::endl << std::endl;
 
   /* generate U*Sigma */
   fmf_Multiply(-1, A, V, 1.0, U, 0.0);
 
   //if (c3db::parall->is_master()) 
   //    std::cout << "AFTER FMF_Multiply" << std::endl;

   /* normalize U*sigma */
   indx = 0;
   for (n = 0; n < (neq[0] + neq[1]); ++n) 
   {
      tmp2[n] = CGrid::cc_pack_idot(1, U+indx, U+indx);
      indx += 2*CGrid::npack1_max();
   }
   c3db::parall->Vector_SumAll(1, neq[0] + neq[1], tmp2);
 
   for (n = 0; n < (neq[0] + neq[1]); ++n)
      tmp2[n] = 1.0 / std::sqrt(tmp2[n]);
 
   indx = 0;
   for (n = 0; n < (neq[0] + neq[1]); ++n) 
   {
      CGrid::c_pack_SMul(1, tmp2[n], U+indx);
      indx += 2 * CGrid::npack1_max();
   }
 
   /* calculated sqrt(S^2) */
   for (n = 0; n < (neq[0] + neq[1]); ++n) 
   {
      if (S[n] < 0.0)
         S[n] = std::fabs(S[n]);
      S[n] = std::sqrt(S[n]);
   }
 
   delete[] tmp2;
}


/*************************************
 *                                   *
 *            Cneb::m_scal           *
 *                                   *
 *************************************/
/**
 * @brief Scale the elements of a matrix by a scalar value.
 *
 * This function scales all elements of the input matrix 'hml' by the specified scalar value 'alpha'.
 *
 * @param[in] alpha The scalar value to scale the matrix by.
 * @param[in,out] hml The input matrix to be scaled, and the resulting scaled matrix.
 */
void Cneb::m_scal(double alpha, double *hml) {
  int one = 1;
  int nsize = ne[0] * ne[0] + ne[1] * ne[1];

  DSCAL_PWDFT(nsize, alpha, hml, one);
}


/*************************************
 *                                   *
 *            Cneb::m_trace          *
 *                                   *
 *************************************/
/**
 * @brief Calculate the trace of a matrix.
 *
 * This function calculates the trace of the input matrix 'hml', which is the sum of the diagonal elements.
 *
 * @param[in] hml The input matrix for which the trace is to be calculated.
 * @return The trace of the matrix.
 */
double Cneb::m_trace(double *hml) 
{
   int ms, i;
   int mshift = 0;
   double sum = 0.0;
   for (ms = 0; ms < ispin; ++ms) 
   {
      for (i = 0; i < ne[ms]; ++i)
         sum += hml[i + i * ne[ms] + mshift];
      mshift += ne[0] * ne[0];
   }
   return sum;
}


/*************************************
 *                                   *
 *        Cneb::m_diagonalize        *
 *                                   *
 *************************************/
/**
 * @brief Diagonalize a matrix and compute its eigenvalues.
 *
 * This function diagonalizes the input matrix 'hml' and computes its eigenvalues, which are stored in the 'eig' array.
 *
 * @param[in,out] hml The input matrix to be diagonalized.
 * @param[out] eig An array to store the computed eigenvalues.
 */
void Cneb::m_diagonalize(double *hml, double *eig) 
{
   nwpw_timing_function ftimer(17);
 
   if (mparallelized)
   {
      std::ostringstream msg;
      msg << "NWPW Error: m_diagonalize() mparallelized is NOT supported\n"
          << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
      throw(std::runtime_error(msg.str()));
   } 
   else 
   {
      int n = ne[0] + ne[1];
      int nn = ne[0]*ne[0] + ne[1]*ne[1];
 
      if (c1db::parall->is_master())
         c3db::mygdevice.NN_eigensolver(ispin, ne, hml, eig);
      c1db::parall->Brdcst_Values(0, 0, nn, hml);
      c1db::parall->Brdcst_Values(0, 0, n, eig);
   }
}


/*************************************
 *                                   *
 *     Cneb::m_scale_s22_s21_s11     *
 *                                   *
 *************************************/
/**
 * @brief Scale matrices S22, S21, and S11 for NEB calculations.
 *
 * This function scales the matrices S22, S21, and S11, which are used in the context of NEB (Nudged Elastic Band) calculations.
 * The scaling depends on the parameters 'mb' and 'dte'. If 'mb' is -1, it scales all matrices for all spin channels; otherwise, it scales matrices for the specified spin channel.
 *
 * @param[in] mb The spin channel index to which the scaling is applied. If -1, scaling is applied to all spin channels.
 * @param[in] dte The scaling factor for the matrices.
 * @param[in,out] s22 The S22 matrix to be scaled.
 * @param[in,out] s21 The S21 matrix to be scaled.
 * @param[in,out] s11 The S11 matrix to be scaled.
 */
void Cneb::m_scale_s22_s21_s11(const int mb, const double dte, double *s22, double *s21, double *s11) 
{
   int j, k, ms, ms1, ms2, ishift2, indx0, indx, indxt;
 
   if (mb == -1) {
     ms1 = 0;
     ms2 = ispin;
     ishift2 = ne[0] * ne[0];
   } else {
     ms1 = mb;
     ms2 = mb + 1;
     ishift2 = 0;
   }
 
   for (ms = ms1; ms < ms2; ++ms) {
     indx0 = ms * ishift2;
     for (k = 0; k < ne[ms]; ++k) {
       s22[indx0] = (1.0 - s22[indx0]) * (0.5 / dte);
       s21[indx0] = (1.0 - s21[indx0]) * (0.5);
       s11[indx0] *= -0.5 * dte;
 
       indx = indx0 + 1;
       indxt = indx0 + ne[ms];
       for (j = (k + 1); j < ne[ms]; ++j) {
         s22[indx] *= (-0.5 / dte);
         s22[indxt] *= (-0.5 / dte);
         s21[indx] *= -0.5;
         s21[indxt] *= -0.5;
         s11[indx] *= -0.5 * dte;
         s11[indxt] *= -0.5 * dte;
 
         indx += 1;
         indxt += ne[ms];
       }
       indx0 += (ne[ms] + 1);
     }
   }
}


/*************************************
 *                                   *
 *   Cneb::m_scale_s22_s21_s12_s11   *
 *                                   *
 *************************************/
/**
 * @brief Scale matrices S22, S21, S12, and S11 for NEB calculations.
 *
 * This function scales the matrices S22, S21, S12, and S11, which are used in the context of NEB (Nudged Elastic Band) calculations.
 * The scaling depends on the parameters 'mb' and 'dte'. If 'mb' is -1, it scales all matrices for all spin channels; otherwise, it scales matrices for the specified spin channel.
 *
 * @param[in] mb The spin channel index to which the scaling is applied. If -1, scaling is applied to all spin channels.
 * @param[in] dte The scaling factor for the matrices.
 * @param[in,out] s22 The S22 matrix to be scaled.
 * @param[in,out] s21 The S21 matrix to be scaled.
 * @param[in,out] s12 The S12 matrix to be scaled.
 * @param[in,out] s11 The S11 matrix to be scaled.
 */
void Cneb::m_scale_s22_s21_s12_s11(const int mb, const double dte, double *s22,
                                   double *s21, double *s12, double *s11) 
{
   int j, k, ms, ms1, ms2, ishift2, indx0, indx, indxt;
 
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift2 = ne[0] * ne[0];
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb + 1;
      ishift2 = 0;
   }
 
   for (ms = ms1; ms < ms2; ++ms) 
   {
      indx0 = ms * ishift2;
      for (k = 0; k < ne[ms]; ++k) 
      {
         s22[indx0] = (1.0 - s22[indx0]) * (0.5 / dte);
         s21[indx0] = (1.0 - s21[indx0]) * (0.5);
         s12[indx0] = (1.0 - s12[indx0]) * (0.5);
         s11[indx0] *= -0.5 * dte;
        
         indx = indx0 + 1;
         indxt = indx0 + ne[ms];
         for (j = (k + 1); j < ne[ms]; ++j) 
         {
            s22[indx] *= (-0.5 / dte);
            s22[indxt] *= (-0.5 / dte);
            s21[indx] *= -0.5;
            s21[indxt] *= -0.5;
            s12[indx] *= -0.5;
            s12[indxt] *= -0.5;
            s11[indx] *= -0.5 * dte;
            s11[indxt] *= -0.5 * dte;
           
            indx += 1;
            indxt += ne[ms];
         }
         indx0 += (ne[ms] + 1);
      }
   }
}


/*************************************
 *                                   *
 *      Cneb::mm_SCtimesVtrans       *
 *                                   *
 *************************************/
/**
 * @brief Multiply a matrix S by the transpose of a matrix Vt, with scaling.
 *
 * This function multiplies a matrix S by the transpose of a matrix Vt while applying scaling factors based on the parameters 'mb' and 't'. If 'mb' is -1, the operation is performed for all spin channels; otherwise, it is performed for the specified spin channel.
 *
 * @param[in] mb The spin channel index to which the operation is applied. If -1, the operation is applied to all spin channels.
 * @param[in] t The scaling factor used for the operation.
 * @param[in] S The input matrix to be multiplied by the transpose of Vt.
 * @param[in] Vt The transpose of the matrix V.
 * @param[out] A The result matrix A.
 * @param[out] B The result matrix B.
 * @param[out] SA The result matrix SA, after applying scaling factors.
 * @param[out] SB The result matrix SB, after applying scaling factors.
 */
void Cneb::mm_SCtimesVtrans(const int mb, const double t, double *S, double *Vt,
                            double *A, double *B, double *SA, double *SB) 
{
   nwpw_timing_function ftimer(19);

   int ms1,ms2,ishift2,ishift1,nj;
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift2 = ne[0] * ne[0];
      ishift1 = ne[0];
      nj = ne[0] + ne[1];
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb + 1;
      ishift2 = 0;
      ishift1 = 0;
      nj = ne[mb];
   }
   for (auto j=0; j<nj; ++j) 
   {
      SA[j] = cos(S[j] * t);
      SB[j] = sin(S[j] * t);
   }
   for (auto ms=ms1; ms<ms2; ++ms) 
   {
      auto shift1 = ms * ishift1;
      auto shift2 = ms * ishift2;
      for (auto k=0; k<ne[ms]; ++k) 
      {
         auto indx1 = shift1;
         auto indx2 = shift2 + k * ne[ms];
         for (auto j=0; j<ne[ms]; ++j) 
         {
            A[indx2] = SA[indx1] * Vt[indx2];
            B[indx2] = SB[indx1] * Vt[indx2];
            ++indx1;
            ++indx2;
         }
      }
   }
}


/*************************************
 *                                   *
 *      Cneb::mm_SCtimesVtrans2      *
 *                                   *
 *************************************/
/**
 * @brief Multiply a matrix S by the transpose of a matrix Vt, with scaling.
 *
 * This function multiplies a matrix S by the transpose of a matrix Vt while applying scaling factors based on the parameters 'mb' and 't'. If 'mb' is -1, the operation is performed for all spin channels; otherwise, it is performed for the specified spin channel.
 *
 * @param[in] mb The spin channel index to which the operation is applied. If -1, the operation is applied to all spin channels.
 * @param[in] t The scaling factor used for the operation.
 * @param[in] S The input matrix to be multiplied by the transpose of Vt.
 * @param[in] Vt The transpose of the matrix V.
 * @param[out] A The result matrix A.
 * @param[out] B The result matrix B.
 * @param[out] SA The result matrix SA, after applying scaling factors.
 * @param[out] SB The result matrix SB, after applying scaling factors.
 */
void Cneb::mm_SCtimesVtrans2(const int mb, const double t, double *S,
                             double *Vt, double *A, double *B, double *SA, double *SB) 
{
   nwpw_timing_function ftimer(19);
   int ms, n, ms1, ms2, ishift2, shift2, ishift1, shift1, nj;
   int j, k, indx1, indx2;
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift2 = ne[0] * ne[0];
      ishift1 = ne[0];
      nj = ne[0] + ne[1];
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb + 1;
      ishift2 = 0;
      ishift1 = 0;
      nj = ne[mb];
   }
   for (j = 0; j < nj; ++j) 
   {
      SA[j] = S[j] * sin(S[j] * t);
      SB[j] = S[j] * cos(S[j] * t);
   }
   for (ms = ms1; ms < ms2; ++ms) 
   {
      shift1 = ms * ishift1;
      shift2 = ms * ishift2;
      for (k = 0; k < ne[ms]; ++k) 
      {
         indx1 = shift1;
         indx2 = shift2 + k * ne[ms];
         for (j = 0; j < ne[ms]; ++j) 
         {
            A[indx2] = SA[indx1] * Vt[indx2];
            B[indx2] = SB[indx1] * Vt[indx2];
            ++indx1;
            ++indx2;
         }
      }
   }
}


/*************************************
 *                                   *
 *      Cneb::mm_SCtimesVtrans3      *
 *                                   *
 *************************************/
/**
 * @brief Multiply a matrix S by the transpose of a matrix Vt with scaling.
 *
 * This function multiplies a matrix S by the transpose of a matrix Vt while applying scaling factors based on the parameters 'mb' and 't'. If 'mb' is -1, the operation is performed for all spin channels; otherwise, it is performed for the specified spin channel.
 *
 * @param[in] mb The spin channel index to which the operation is applied. If -1, the operation is applied to all spin channels.
 * @param[in] t The scaling factor used for the operation.
 * @param[in] S The input matrix to be multiplied by the transpose of Vt.
 * @param[in] Vt The transpose of the matrix V.
 * @param[out] A The result matrix A.
 * @param[out] B The result matrix B.
 * @param[out] SA The result matrix SA, after applying scaling factors.
 * @param[out] SB The result matrix SB, after applying scaling factors.
 */
void Cneb::mm_SCtimesVtrans3(const int mb, const double t, double *S,
                             double *Vt, double *A, double *B, double *SA, double *SB) 
{
   nwpw_timing_function ftimer(19);
   int ms, n, ms1, ms2, ishift2, shift2, ishift1, shift1, nj;
   int j, k, indx1, indx2;
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift2 = ne[0] * ne[0];
      ishift1 = ne[0];
      nj = ne[0] + ne[1];
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb + 1;
      ishift2 = 0;
      ishift1 = 0;
      nj = ne[mb];
   }
   for (j = 0; j < nj; ++j) 
   {
      SA[j] = sin(S[j] * t);
      SB[j] = 1.0 - cos(S[j] * t);
   }
   for (ms = ms1; ms < ms2; ++ms) 
   {
      shift1 = ms * ishift1;
      shift2 = ms * ishift2;
      for (k = 0; k < ne[ms]; ++k) 
      {
         indx1 = shift1;
         indx2 = shift2 + k * ne[ms];
         for (j = 0; j < ne[ms]; ++j) 
         {
            A[indx2] = SA[indx1] * Vt[indx2];
            B[indx2] = SB[indx1] * Vt[indx2];
            ++indx1;
            ++indx2;
         }
      }
   }
}


/*************************************
 *                                   *
 *         Cneb::mmm_Multiply        *
 *                                   *
 *************************************/
void Cneb::mmm_Multiply(const int mb, double *a, double *b, double alpha,
                        double *c, double beta) 
{
   nwpw_timing_function ftimer(18);
   int ms, n, ms1, ms2, ishift2, shift2;
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift2 = ne[0] * ne[0];
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb + 1;
      ishift2 = 0;
   }
   for (ms = ms1; ms < ms2; ++ms) 
   {
      n = ne[ms];
      if (n > 0) 
      {
         shift2 = ms * ishift2;
     
         DGEMM_PWDFT((char *)"N", (char *)"N", n, n, n, alpha, a + shift2, n,
                     b + shift2, n, beta, c + shift2, n);
      }
   }
}

/*************************************
 *                                   *
 *         Cneb::mmm_Multiply2       *
 *                                   *
 *************************************/
void Cneb::mmm_Multiply2(const int mb, double *a, double *b, double alpha,
                         double *c, double beta) {
  nwpw_timing_function ftimer(18);
  int ms, n, ms1, ms2, ishift2, shift2;
  if (mb == -1) {
    ms1 = 0;
    ms2 = ispin;
    ishift2 = ne[0] * ne[0];
  } else {
    ms1 = mb;
    ms2 = mb + 1;
    ishift2 = 0;
  }
  for (ms = ms1; ms < ms2; ++ms) {
    n = ne[ms];
    if (n > 0) {
      shift2 = ms * ishift2;

      DGEMM_PWDFT((char *)"T", (char *)"N", n, n, n, alpha, a + shift2, n,
                  b + shift2, n, beta, c + shift2, n);
    }
  }
}

void Cneb::mm_transpose(const int mb, double *a, double *b) 
{
   int i, j, indx, indxt;
   int ms, n, ms1, ms2, ishift2, shift2;
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift2 = ne[0] * ne[0];
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb + 1;
      ishift2 = 0;
   }
   for (ms = ms1; ms < ms2; ++ms) 
   {
      shift2 = ms * ishift2;
      for (j = 0; j < ne[ms]; ++j)
        for (i = 0; i < ne[ms]; ++i) 
        {
           indx  = i + j * ne[ms] + shift2;
           indxt = j + i * ne[ms] + shift2;
           b[indx] = a[indxt];
        }
   }
}

/********************************
 *                              *
 *   Cneb::mm_Kiril_Btransform  *
 *                              *
 ********************************/
void Cneb::mm_Kiril_Btransform(const int mb, double *a, double *b) {
  int ms1, ms2, ishift2;
  if (mb == -1) {
    ms1 = 0;
    ms2 = ispin;
    ishift2 = ne[0] * ne[0];
  } else {
    ms1 = mb;
    ms2 = mb + 1;
    ishift2 = 0;
  }
  for (auto ms = ms1; ms < ms2; ++ms) {
    int shift2 = ms * ishift2;
    for (auto i = 0; i < ne[ms]; ++i)
      for (auto j = 0; j < i; ++j) {
        int indx = i + j * i + shift2;
        double tmp = 0.5 * (a[indx] + b[indx]);
        a[indx] = tmp;
        b[indx] = tmp;
      }
  }
}

/********************************
 *                              *
 *     Cneb::ggm_lambda         *
 *                              *
 ********************************/

#define ITERLMD 220
#define CONVGLMD 1e-15
#define CONVGLMD2 1e-12

// Lagrange multiplier (expensive method)
void Cneb::ggm_lambda(double dte, double *psi1, double *psi2, double *lmbda) 
{
   nwpw_timing_function ftimer(3);
 
   int one = 1;
   double rmone = -1.0;
   double adiff = 0.0;
 
   for (int ms = 0; ms < ispin; ++ms) {
 
     int nn = m_size(ms);
 
     ffm3_sym_Multiply(ms, psi1, psi2, s11, s21, s22);
     m_scale_s22_s21_s11(ms, dte, s22, s21, s11);
 
     int jj;
     int ii = 0;
     int done = 0;
 
     // DCOPY_PWDFT(nn, s21, one, s12, one);
     // DCOPY_PWDFT(nn, s22, one, sa0, one);
     std::memcpy(s12, s21, nn * sizeof(double));
     std::memcpy(sa0, s22, nn * sizeof(double));
 
     while ((!done) && ((ii++) < ITERLMD)) {
       // DCOPY_PWDFT(nn, s22, one, sa1, one);
       std::memcpy(sa1, s22, nn * sizeof(double));
 
       // mmm_Multiply(ms, s21, sa0, 1.0, sa1, 1.0);
       // mmm_Multiply(ms, sa0, s12, 1.0, sa1, 1.0);
       // mmm_Multiply(ms, s11, sa0, 1.0, st1, 0.0);
       // mmm_Multiply(ms, sa0, st1, 1.0, sa1, 1.0);
       c3db::mygdevice.MM6_dgemm(ne[ms], s12, s12, s11, sa0, sa1, st1);
 
       // DCOPY_PWDFT(nn, sa1, one, st1, one);
       std::memcpy(st1, sa1, nn * sizeof(double));
       DAXPY_PWDFT(nn, rmone, sa0, one, st1, one);
       jj = IDAMAX_PWDFT(nn, st1, one);
       adiff = fabs(st1[IDAMAX_PWDFT(nn, st1, one) - 1]);
 
       if (adiff < CONVGLMD)
         done = 1;
       else
         std::memcpy(sa0, sa1,
                     nn *
                         sizeof(double)); // DCOPY_PWDFT(nn, sa1, one, sa0, one);
     }
     // printf("ierr=10 check nn=%d jj=%d adiff=%le ii=%d
     // done=%d\n",nn,jj,adiff,ii,done);
 
     if (adiff > CONVGLMD2) {
       if (!done)
         printf("ierr=10 adiff=%le\n", adiff);
     }
 
     // DCOPY_PWDFT(nn, sa1, one, &lmbda[ms*ne[0]*ne[0]], one);
     std::memcpy(&lmbda[ms*ne[0]*ne[0]],sa1,nn*sizeof(double));
     // std::memcpy(lmbda+ms*ne[0]*ne[0],sa1,nn*sizeof(double));
 
   } // for loop - ms
 
   /* correction due to contraint */
   fmf_Multiply(-1, psi1, lmbda, dte, psi2, 1.0);
}

/********************************
 *                              *
 *     Cneb::ggm_lambda_sic     *
 *                              *
 ********************************/
// Lagrange multiplier for Stiefel and  SIC and(expensive method)
void Cneb::ggm_lambda_sic(double dte, double *psi1, double *psi2,
                          double *lmbda) {
  nwpw_timing_function ftimer(3);

  int one = 1;
  double rmone = -1.0;
  double adiff = 0.0;

  for (int ms = 0; ms < ispin; ++ms) {

    int nn = m_size(ms);

    ffm4_sym_Multiply(ms, psi1, psi2, s11, s21, s12, s22);
    mm_Kiril_Btransform(ms, s12, s21);

    m_scale_s22_s21_s12_s11(ms, dte, s22, s21, s12, s11);

    int jj;
    int ii = 0;
    int done = 0;

    // DCOPY_PWDFT(nn, s21, one, s12, one);
    // DCOPY_PWDFT(nn, s22, one, sa0, one);
    std::memcpy(s12, s21, nn * sizeof(double));
    std::memcpy(sa0, s22, nn * sizeof(double));

    while ((!done) && ((ii++) < ITERLMD)) {
      // DCOPY_PWDFT(nn, s22, one, sa1, one);
      std::memcpy(sa1, s22, nn * sizeof(double));

      // mmm_Multiply(ms, s21, sa0, 1.0, sa1, 1.0);
      // mmm_Multiply(ms, sa0, s12, 1.0, sa1, 1.0);
      // mmm_Multiply(ms, s11, sa0, 1.0, st1, 0.0);
      // mmm_Multiply(ms, sa0, st1, 1.0, sa1, 1.0);
      c3db::mygdevice.MM6_dgemm(ne[ms], s12, s12, s11, sa0, sa1, st1);

      // DCOPY_PWDFT(nn, sa1, one, st1, one);
      std::memcpy(st1, sa1, nn * sizeof(double));
      DAXPY_PWDFT(nn, rmone, sa0, one, st1, one);
      jj = IDAMAX_PWDFT(nn, st1, one);
      adiff = fabs(st1[IDAMAX_PWDFT(nn, st1, one) - 1]);

      if (adiff < CONVGLMD)
        done = 1;
      else
        std::memcpy(sa0, sa1,
                    nn *
                        sizeof(double)); // DCOPY_PWDFT(nn, sa1, one, sa0, one);
    }
    // printf("ierr=10 check nn=%d jj=%d adiff=%le ii=%d
    // done=%d\n",nn,jj,adiff,ii,done);

    if (adiff > CONVGLMD2) {
      if (!done)
        printf("ierr=10 adiff=%le\n", adiff);
    }

    // DCOPY_PWDFT(nn, sa1, one, &lmbda[ms*ne[0]*ne[0]], one);
    std::memcpy(&lmbda[ms * ne[0] * ne[0]], sa1, nn * sizeof(double));
    // std::memcpy(lmbda+ms*ne[0]*ne[0],sa1,nn*sizeof(double));

  } // for loop - ms

  /* correction due to contraint */
  fmf_Multiply(-1, psi1, lmbda, dte, psi2, 1.0);
}

/********************************
 *                              *
 *        Cneb::g_ortho         *
 *                              *
 ********************************/
/*
   Performs a Gram-Schmidt orthogonalization on psi
*/
void Cneb::g_ortho(double *psi) 
{
   int indxj, indxk, ishift;
   double w;
   if (parallelized) 
   {
      //std::ostringstream msg;
      //msg << "NWPW Error: g_ortho() parallelized is NOT supported\n"
      //    << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
      //throw(std::runtime_error(msg.str()));

      int np_j = c1db::parall->np_j();
      int taskid_j = c1db::parall->taskid_j();
      int taskid_k = c1db::parall->taskid_k();

      int npack2 = 2*CGrid::npack1_max();
      double *tmp = new (std::nothrow) double[npack2]();

      for (auto nbq=0; nbq<nbrillq; ++nbq)
      {
         int shiftk = nbq*(neq[0]+neq[1])*npack2; 
         for (auto ms=0; ms<ispin; ++ms)
         {
            auto shift0 = ms*neq[0]*npack2;
            auto kcur = np_j-1;
            auto kk   = na[ms][kcur]-1;
     
            for (auto k=ne[ms]-1; k>=0; --k)
            {
               if (kcur==taskid_j)
               {
                  int indxk = npack2*kk + shift0 + shiftk;
                  double w = CGrid::cc_pack_dot(nbq+1, psi+indxk, psi+indxk);
                  w = 1.0/std::sqrt(w);
                  CGrid::c_pack_SMul(nbq+1, w, psi+indxk);
                  std::memcpy(tmp,psi+indxk,npack2*sizeof(double));
               }
     
               if (kcur>0)
                   c1db::parall->Brdcst_Values(2,kcur,npack2,tmp);
     
               //*** set j = k+1 ***
               auto jj   = kk;
               auto jcur = kcur;
     
               --jj;
               if (jj<0) 
               {
                  --jcur;
                  jj = na[ms][jcur] - 1;
               }
     
               for (auto j=k-1; j>=0; --j)
               {
                  if (jcur==taskid_j) 
                  {
                     int indxj = npack2*jj + shift0 + shiftk;
                     double w = -CGrid::cc_pack_dot(nbq+1, tmp, psi+indxj);
                     CGrid::cc_pack_daxpy(nbq+1, w, tmp, psi+indxj);
     
                  }
     
                  --jj;
                  if (jj<0) 
                  {
                     --jcur;
                     jj = na[ms][jcur] - 1;
                  }
               }
     
               --kk;
               if (kk<0) 
               {
                  --kcur;
                  kk = na[ms][kcur] - 1;
               }
            }
         }
      }

      delete[] tmp;
   } 
   else 
   {
      // npj==1
      int npack2 = 2*CGrid::npack1_max();

      for (auto nbq=0; nbq<nbrillq; ++nbq)
      {
         int ishiftk = nbq*ispin*(neq[0]+neq[1])*npack2; 
         for (auto ms=0; ms<ispin; ++ms) 
         {
            int ishift = ms*ne[0]*npack2;
            for (auto k=ne[ms]-1; k>=0; --k) 
            {
               int indxk = npack2*k + ishift + ishiftk;
               double w = CGrid::cc_pack_dot(nbq+1, psi+indxk, psi+indxk);
               w = 1.0/std::sqrt(w);
               CGrid::c_pack_SMul(nbq+1, w, psi+indxk);
              
               for (auto j=k-1; j>=0; --j) 
               {
                  int indxj = npack2*j + ishift + ishiftk;
                  double w = -CGrid::cc_pack_dot(nbq+1, psi+indxk, psi+indxj);
                  CGrid::cc_pack_daxpy(nbq+1, w, psi+indxk, psi+indxj);
               }
            }
         }
      }
   }
}

/********************************
 *                              *
 *        Cneb::fm_QR           *
 *                              *
 ********************************/
/*
   Performs a modified Gram-Schmidt QR.
*/
void Cneb::fm_QR(const int mb, double *Q, double *R) {
  int n, ms1, ms2, ishift2, shift2;
  int ishift, indxj, indxk, indxm;
  double w;
  if (mb == -1) {
    ms1 = 0;
    ms2 = ispin;
    ishift2 = ne[0] * ne[0];
    std::memset(R, 0, (ne[0] * ne[0] + ne[1] * ne[1]) * sizeof(double));
  } else {
    ms1 = mb;
    ms2 = mb + 1;
    ishift2 = 0;
    std::memset(R, 0, (ne[mb] * ne[mb]) * sizeof(double));
  }

  // npj>1
  if (parallelized) {
    std::ostringstream msg;
    msg << "NWPW Error: fm_QR parallelized is NOT supported\n"
        << "\t - " << __FILE__ << " : " << __LINE__ << std::endl;
    throw(std::runtime_error(msg.str()));
  }

  // npj==1
  else {
    for (auto ms = ms1; ms < ms2; ++ms) {
      ishift = ms * ne[0] * 2 * CGrid::npack1_max();
      for (auto k = 0; k < ne[ms]; ++k) {
        indxk = 2 * CGrid::npack1_max() * k + ishift;
        indxm = k + k * ne[ms] + ms * ishift2;
        w = CGrid::cc_pack_dot(1, Q + indxk, Q + indxk);
        w = sqrt(w);
        R[indxm] = w;
        w = 1.0 / w;
        CGrid::c_pack_SMul(1, w, Q + indxk);

        for (auto j = k + 1; j < ne[ms]; ++j) {
          indxj = 2 * CGrid::npack1_max() * j + ishift;
          indxm = k + j * ne[ms] + ms * ishift2;
          w = CGrid::cc_pack_dot(1, Q + indxk, Q + indxj);
          R[indxm] = w;
          CGrid::cc_pack_daxpy(1, -w, Q + indxk, Q + indxj);
        }
      }
    }
  }
}

/********************************
 *                              *
 *     Cneb::mmm4_AR_to_T4      *
 *                              *
 ********************************/
void Cneb::mmm4_AR_to_T4(const int mb, const double *A, const double *R, double *T4) 
{
   int ms1, ms2, ishift1, ishift2;
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift1 = ne[0] * ne[0];
      ishift2 = 4 * ne[0] * ne[0];
      std::memset(T4, 0, (4 * ne[0] * ne[0] + ne[1] * ne[1]) * sizeof(double));
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb;
      ishift1 = 0;
      ishift2 = 0;
      std::memset(T4, 0, (4 * ne[mb] * ne[mb]) * sizeof(double));
   }

   for (auto ms = ms1; ms < ms2; ++ms) 
   {
      const int n = ne[ms];
      const double *Asub = A + ms * ishift1;
      const double *Rsub = R + ms * ishift1;
      double *Tsub = T4 + ms * ishift2;
     
      //**** copy A to upper-left of T ****
      for (auto j = 0; j < n; ++j)
         for (auto i = 0; i < n; ++i)
            Tsub[i + j * (2 * n)] = Asub[i + j * n];
     
      //**** copy R to lower-left of T ****
      for (auto j = 0; j < n; ++j)
         for (auto i = 0; i < n; ++i)
            Tsub[(i + n) + j * (2 * n)] = Rsub[i + j * n];
     
      //**** copy -R^t to upper-right of T ****
      for (auto j = 0; j < n; ++j)
         for (auto i = 0; i < n; ++i)
            Tsub[i + (j + n) * (2 * n)] = -Rsub[j + i * n];
   }
}

/********************************
 *                              *
 *     Cneb::m4_FactorSkew      *
 *                              *
 ********************************/
void Cneb::m4_FactorSkew(const int mb, double *K4, double *V4, double *W4,
                         double *Sigma) {
  int ms1, ms2, ishift1, ishift2;
  if (mb == -1) {
    ms1 = 0;
    ms2 = ispin;
    ishift1 = 2 * ne[0];
    ishift2 = 4 * ne[0] * ne[0];
  } else {
    ms1 = mb;
    ms2 = mb;
    ishift1 = 0;
    ishift2 = 0;
  }

  for (auto ms = ms1; ms < ms2; ++ms) {
    int n = 2 * ne[ms];
    int shift1 = ms * ishift1;
    int shift2 = ms * ishift2;

    FACTOR_SKEW_PWDFT(n, K4 + shift2, V4 + shift2, W4 + shift2, Sigma + shift1);
  }
}

/********************************
 *                              *
 *    mm4_RotationSkew_sub1     *
 *                              *
 ********************************/
 /*
  Description:
    This function calculates the sine and cosine values of the scaled Sigma values multiplied by 't',
    and stores the results in arrays SA and SB. The operation is performed element-wise for the given size 'n'.

  Parameters:
    - n: Size of the arrays.
    - t: Scaling factor for the rotation angle.
    - Sigma: Pointer to a double array containing input matrix data.
    - SA: Pointer to a double array where the cosine results will be stored.
    - SB: Pointer to a double array where the sine results will be stored.
*/
static void mm4_RotateSkew_sub1(const int n, const double t,
                                const double *Sigma, double *SA, double *SB) 
{
   for (auto i = 0; i < n; ++i) 
   {
      SA[i] = cos(Sigma[i] * t);
      SB[i] = sin(Sigma[i] * t);
   }
}

/********************************
 *                              *
 *    mm4_RotationSkew_sub2     *
 *                              *
 ********************************/
 /*
  Description:
    This function performs a rotation and skew transformation on matrices V and W using the precalculated
    sine and cosine values from arrays SA and SB. The resulting matrices A and B are stored accordingly.
    The operation is performed element-wise for the given size 'n'.

  Parameters:
    - n: Size of the arrays.
    - SA, SB: Pointers to double arrays containing precalculated sine and cosine values.
    - V, W: Pointers to double arrays containing input matrix data.
    - A: Pointer to a double array where the rotated matrix A will be stored.
    - B: Pointer to a double array where the skewed matrix B will be stored.
*/
static void mm4_RotateSkew_sub2(const int n, const double *SA, const double *SB,
                                const double *V, const double *W, double *A,
                                double *B) {
  for (auto j = 0; j < n; ++j)
    for (auto i = 0; i < n; ++i) {
      int indx = i + j * n;
      A[indx] = V[indx] * SA[j] + W[indx] * SB[j];
      B[indx] = W[indx] * SA[j] - V[indx] * SB[j];
    }
}

/********************************
 *                              *
 *    Cneb::m4_RotationSkew     *
 *                              *
 ********************************/
 /*
  Description:
    This function performs a rotation and skew transformation on given matrices V4 and W4, storing the
    results in matrices A4, B4, and R4. The transformation is based on the provided parameters and
    matrix properties. The operation is performed within specified matrix blocks determined by 'mb' and
    matrix sizes 'ne'.

  Parameters:
    - mb: Index specifying the block of matrices to operate on. If mb is -1, the entire matrix V4 and W4
          are used for calculations. Otherwise, a subset of matrices is used.
    - t: Rotation angle for the transformation.
    - V4, W4: Pointers to double arrays containing input matrix data.
    - Sigma: Pointer to a double array for storing intermediate transformation values.
    - A4, B4: Pointers to double arrays where the resulting matrices A4 and B4 will be stored.
    - R4: Pointer to a double array where the final transformed matrix R4 will be stored.
*/

void Cneb::m4_RotationSkew(const int mb, const double t, double *V4, double *W4,
                           double *Sigma, double *A4, double *B4, double *R4) {
  int ms1, ms2, ishift1, ishift2, nj;
  if (mb == -1) {
    ms1 = 0;
    ms2 = ispin;
    ishift1 = 2 * ne[0];
    ishift2 = 4 * ne[0] * ne[0];
    nj = 2 * (ne[0] + ne[1]);
  } else {
    ms1 = mb;
    ms2 = mb;
    ishift1 = 0;
    ishift2 = 0;
    nj = 2 * ne[mb];
  }

  double rzero = 0.0;
  double rone = 1.0;
  double SA[nj];
  double SB[nj];

  mm4_RotateSkew_sub1(nj, t, Sigma, SA, SB);

  for (auto ms = ms1; ms < ms2; ++ms) {
    int n = 2 * ne[ms];
    int shift1 = ms * ishift1;
    int shift2 = ms * ishift2;
    mm4_RotateSkew_sub2(n, SA + shift1, SB + shift1, V4 + shift2, W4 + shift2,
                        A4 + shift2, B4 + shift2);

    DGEMM_PWDFT((char *)"N", (char *)"T", n, n, n, rone, V4 + shift2, n,
                A4 + shift2, n, rzero, R4 + shift2, n);
    DGEMM_PWDFT((char *)"N", (char *)"T", n, n, n, rone, W4 + shift2, n,
                B4 + shift2, n, rone, R4 + shift2, n);
  }
}

/********************************
 *                              *
 *     Cneb::m4_R4_to_MN        *
 *                              *
 ********************************/
/*
  Description:
    This function converts a given matrix R4 to two matrices M and N. The matrices M and N are
    extracted from specific subblocks of R4 based on the provided index 'mb' and matrix sizes 'ne'.
    The operation is performed according to the specified parameters and matrix properties.
    
  Parameters:
    - mb: Index specifying the block of matrices to operate on. If mb is -1, the entire matrix R4 is
          used to extract M and N. Otherwise, a subset of matrices is used for calculations.
    - R4: Pointer to a double array containing the input matrix data (R4).
    - M: Pointer to a double array where the resulting matrix M will be stored.
    - N: Pointer to a double array where the resulting matrix N will be stored.
*/
void Cneb::m4_R4_to_MN(const int mb, const double *R4, double *M, double *N) 
{
   int ms1, ms2, ishift1, ishift2;
   if (mb == -1) 
   {
      ms1 = 0;
      ms2 = ispin;
      ishift1 = ne[0] * ne[0];
      ishift2 = 4 * ne[0] * ne[0];
   } 
   else 
   {
      ms1 = mb;
      ms2 = mb;
      ishift1 = 0;
      ishift2 = 0;
   }
 
   for (auto ms = ms1; ms < ms2; ++ms) 
   {
      int n = ne[ms];
      int shift1 = ms * ishift1;
      int shift2 = ms * ishift2;
     
      for (auto j = 0; j < n; ++j)
         for (auto i = 0; i < n; ++i) 
         {
            M[i + j * n] = R4[i + j * (2 * n)];
            N[i + j * n] = R4[(i + n) + j * (2 * n)];
         }
   }
}

} // namespace pwdft
