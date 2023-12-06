/* Strfac.C -
   Author - Eric Bylaska
*/

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "CStrfac.hpp"

namespace pwdft {

/* Constructors */

/*********************************
 *                               *
 *        CStrfac::CStrfac       *
 *                               *
 *********************************/

CStrfac::CStrfac(Ion *inion, CGrid *ingrid) 
{
   int *ii_indx, *jj_indx, *kk_indx;
 
   mygrid = ingrid;
   myion = inion;
   int tnp = mygrid->c3db::parall->np();
   int tid = mygrid->c3db::parall->taskid();
   Lattice *lattice = mygrid->lattice;
 
   int nx = mygrid->nx;
   int ny = mygrid->ny;
   int nz = mygrid->nz;
 
   for (auto j=0; j<3; ++j)
   for (auto i=0; i<3; ++i) 
   {
      unitg[i+j*3] = lattice->unitg(i, j);
      unita[i+j*3] = lattice->unita(i, j);
   }
 
   maxsize = mygrid->nbrillq+1;
   i_indx  = new (std::nothrow) int* [maxsize]();
   j_indx  = new (std::nothrow) int* [maxsize]();
   k_indx  = new (std::nothrow) int* [maxsize]();
 
   /* allocate memory */
   wx1 = new (std::nothrow) double[2 * (myion->nion) * (mygrid->nx)]();
   wy1 = new (std::nothrow) double[2 * (myion->nion) * (mygrid->ny)]();
   wz1 = new (std::nothrow) double[2 * (myion->nion) * (mygrid->nz)]();
 
   for (auto nb=0; nb<maxsize; ++nb)
   {
      i_indx[nb] = new (std::nothrow) int[mygrid->npack(0)]();
      j_indx[nb] = new (std::nothrow) int[mygrid->npack(0)]();
      k_indx[nb] = new (std::nothrow) int[mygrid->npack(0)]();
   }
   
   ii_indx = new (std::nothrow) int[mygrid->nfft3d]();
   jj_indx = new (std::nothrow) int[mygrid->nfft3d]();
   kk_indx = new (std::nothrow) int[mygrid->nfft3d]();
   for (auto nb=0; nb<maxsize; ++nb) 
   {
      for (auto k=0; k<nz; ++k)
      for (auto j=0; j<ny; ++j)
      for (auto i=0; i<nx; ++i) 
      {
         int p = mygrid->cijktop(i, j, k);
         if (p==tid) 
         {
            int indx = mygrid->cijktoindex(i, j, k);
            ii_indx[indx] = i;
            jj_indx[indx] = j;
            kk_indx[indx] = k;
         }
      }
      mygrid->i_pack(nb, ii_indx);
      mygrid->i_pack(nb, jj_indx);
      mygrid->i_pack(nb, kk_indx);
      mygrid->ii_pack_copy(nb, ii_indx, i_indx[nb]);
      mygrid->ii_pack_copy(nb, jj_indx, j_indx[nb]);
      mygrid->ii_pack_copy(nb, kk_indx, k_indx[nb]);
   }
 
   delete[] kk_indx;
   delete[] jj_indx;
   delete[] ii_indx;
}

/*********************************
 *                               *
 *         CStrfac::phafac       *
 *                               *
 *********************************/
void CStrfac::phafac()
{
   int i, k, nxh, nyh, nzh, nx, ny, nz;
   double a, b, sw1, sw2, sw3, pi;
   double cw1x, cw2x, cw3x;
   double cw1y, cw2y, cw3y;
 
   pi = 4.00 * std::atan(1.0);
 
   nx = (mygrid->nx);
   ny = (mygrid->ny);
   nz = (mygrid->nz);
   nxh = nx / 2;
   nyh = ny / 2;
   nzh = nz / 2;
 
   for (i = 0; i < (myion->nion); ++i) 
   {
      sw1 = unitg[0]*myion->rion1[0+3*i] +
            unitg[1]*myion->rion1[1+3*i] +
            unitg[2]*myion->rion1[2+3*i] + pi;
      sw2 = unitg[3]*myion->rion1[0+3*i] +
            unitg[4]*myion->rion1[1+3*i] +
            unitg[5]*myion->rion1[2+3*i] + pi;
      sw3 = unitg[6]*myion->rion1[0+3*i] +
            unitg[7]*myion->rion1[1+3*i] +
            unitg[8]*myion->rion1[2+3*i] + pi;
     
      cw1x =  std::cos(sw1);
      cw1y = -std::sin(sw1);
      cw2x =  std::cos(sw2);
      cw2y = -std::sin(sw2);
      cw3x =  std::cos(sw3);
      cw3y = -std::sin(sw3);
      wx1[2*i*nx]   = 1.0;
      wx1[2*i*nx+1] = 0.0;
      wy1[2*i*ny]   = 1.0;
      wy1[2*i*ny+1] = 0.0;
      wz1[2*i*nz]   = 1.0;
      wz1[2*i*nz+1] = 0.0;
      for (k=1; k<=nxh; ++k) 
      {
         a = wx1[2 * (k - 1 + i * nx)];
         b = wx1[2 * (k - 1 + i * nx) + 1];
         wx1[2 * (k + i * nx)] = a * cw1x - b * cw1y;
         wx1[2 * (k + i * nx) + 1] = a * cw1y + b * cw1x;
         wx1[2 * (nx - k + i * nx)] = wx1[2 * (k + i * nx)];
         wx1[2 * (nx - k + i * nx) + 1] = -wx1[2 * (k + i * nx) + 1];
      }
     
      for (k=1; k<=nyh; ++k)
      {
         a = wy1[2 * (k - 1 + i * ny)];
         b = wy1[2 * (k - 1 + i * ny) + 1];
         wy1[2 * (k + i * ny)] = a * cw2x - b * cw2y;
         wy1[2 * (k + i * ny) + 1] = a * cw2y + b * cw2x;
         wy1[2 * (ny - k + i * ny)] = wy1[2 * (k + i * ny)];
         wy1[2 * (ny - k + i * ny) + 1] = -wy1[2 * (k + i * ny) + 1];
      }
      for (k=1; k<=nzh; ++k)
      {
         a = wz1[2 * (k - 1 + i * nz)];
         b = wz1[2 * (k - 1 + i * nz) + 1];
         wz1[2 * (k + i * nz)] = a * cw3x - b * cw3y;
         wz1[2 * (k + i * nz) + 1] = a * cw3y + b * cw3x;
         wz1[2 * (nz - k + i * nz)] = wz1[2 * (k + i * nz)];
         wz1[2 * (nz - k + i * nz) + 1] = -wz1[2 * (k + i * nz) + 1];
      }
     
      wx1[2*(nxh+i*nx)]     = 0.0;
      wx1[2*(nxh+i*nx) + 1] = 0.0;
      wy1[2*(nyh+i*ny)]     = 0.0;
      wy1[2*(nyh+i*ny) + 1] = 0.0;
      wz1[2*(nzh+i*nz)]     = 0.0;
      wz1[2*(nzh+i*nz) + 1] = 0.0;
   }
}

/*********************************
 *                               *
 *      CStrfac::strfac_pack     *
 *                               *
 *********************************/
void CStrfac::strfac_pack(const int nb, const int ii, double *strx) 
{
   int npack = mygrid->npack(nb);
   int nx = mygrid->nx;
   int ny = mygrid->ny;
   int nz = mygrid->nz;
 
   const int *indxi = i_indx[nb];
   const int *indxj = j_indx[nb];
   const int *indxk = k_indx[nb];
 
   const double *exi = &wx1[2*ii*nx];
   const double *exj = &wy1[2*ii*ny];
   const double *exk = &wz1[2*ii*nz];
 
   double ai, aj, ak, bi, bj, bk;
   double c, d;
   for (int i = 0; i < npack; ++i)
   {
      ai = exi[2*indxi[i]];
      bi = exi[2*indxi[i] + 1];
      aj = exj[2*indxj[i]];
      bj = exj[2*indxj[i] + 1];
      ak = exk[2*indxk[i]];
      bk = exk[2*indxk[i] + 1];
      c = aj*ak - bj*bk;
      d = aj*bk + ak*bj;
      strx[2*i]   = (ai*c - bi*d);
      strx[2*i+1] = (ai*d + bi*c);
   }
}

} // namespace pwdft