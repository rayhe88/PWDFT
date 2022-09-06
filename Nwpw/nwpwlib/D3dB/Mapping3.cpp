/* Mapping3.C
   Author - Eric Bylaska

	this class is used for defining 3d parallel maps
*/

/*
#include        <iostream>
#include        <cmath>
#include        <cstdlib>

*/

#include        <iostream>
#include	"hilbert.hpp"
#include	"Mapping3.hpp"

namespace pwdft {


/***********************************
 *                                 *
 *        generate_map_indexes     *
 *                                 *
 ***********************************/
static int  generate_map_indexes(int taskid,int np,
                                 int ny,    int nz,
                                 int *pmap, int *qmap)
{
   int nq,q,p;
   int *indx_proc;
   int *indx_q;
   int nq1        = (ny*nz)/np;
   int rmdr1      = (ny*nz)%np;
   int nq2        = nq1;


   indx_proc = new (std::nothrow) int[ny*nz]();
   indx_q    = new (std::nothrow) int[ny*nz]();

   if (rmdr1>0) ++nq2;
   nq=0; p=0; q=0;
   for (int i=0; i<(ny*nz); ++i)
   {
      indx_proc[i] = p;
      indx_q[i]    = q;
      if (taskid==p) ++nq;
      ++q;
      if (q>=nq2)
      {
          q=0;
          ++p;
          p = p%np;
          if (p>=rmdr1) nq2=nq1;
      }
   }

   for (int k=0; k<nz; ++k)
   for (int j=0; j<ny; ++j)
   {
      p = indx_proc[pmap[j+k*ny]];
      q = indx_q[pmap[j+k*ny]];
      pmap[j+k*ny] = p;
      qmap[j+k*ny] = q;
   }
   delete [] indx_proc;
   delete [] indx_q;

   return nq;
}

/***********************************
 *                                 *
 *        expand_hilbert2d         *
 *                                 *
 ***********************************/ 
//integer pmap_in(ny,nz)
//integer qmap_in(ny,nz)
//integer pmap_out(2*ny,2*nz)
//integer qmap_out(2*ny,2*nz)

static void expand_hilbert2d(const int np, const int ny, const int nz,
                             const int pmap_in[], const int qmap_in[],
                                   int pmap_out[],      int qmap_out[])
{
   for (auto k=0; k<nz; ++k)
   for (auto j=0; j<ny; ++j)
   {
      pmap_out[j    + (k)   *2*ny] = pmap_in[j+k*ny];
      pmap_out[j+ny + (k)   *2*ny] = pmap_in[j+k*ny];
      pmap_out[j    + (k+nz)*2*ny] = pmap_in[j+k*ny];
      pmap_out[j+ny + (k+nz)*2*ny] = pmap_in[j+k*ny];
   }

   for (auto p=0; p<np; ++p)
   {
      int nqp = 0;
      for (auto k=0; k<nz; ++k)
      for (auto j=0; j<ny; ++j)
         if (pmap_in[j+k*ny]==p)
            if (qmap_in[j+k*ny]>nqp) 
               nqp = qmap_in[j+k*ny];

      for (auto k=0; k<nz; ++k)
      for (auto j=0; j<ny; ++j)
         if (pmap_in[j+k*ny]==p)
         {
            qmap_out[j    + (k)   *2*ny] = qmap_in[j+k*ny];
            qmap_out[j+ny + (k)   *2*ny] = qmap_in[j+k*ny] + nqp;
            qmap_out[j    + (k+nz)*2*ny] = qmap_in[j+k*ny] + 2*nqp;
            qmap_out[j+ny + (k+nz)*2*ny] = qmap_in[j+k*ny] + 3*nqp;
         }
   }
}



/********************************
 *                              *
 *         Constructors         *
 *                              *
 ********************************/

Mapping3::Mapping3()
{
   maptype=0; n2ft3d=0; nfft3d=0;
   nx=0; ny=0; nz=0;
   qmap[0] = NULL; qmap[1] = NULL; qmap[2] = NULL;
   pmap[0] = NULL; pmap[1] = NULL; pmap[2] = NULL;
   kmap = NULL;
}

Mapping3::Mapping3(const int mapin, const int npin, const int taskidin, 
                   const int nxin,  const int nyin, const int nzin)
{
   int p,q;

   qmap[0] = NULL; qmap[1] = NULL; qmap[2] = NULL;
   pmap[0] = NULL; pmap[1] = NULL; pmap[2] = NULL;
   kmap = NULL;

   np      = npin;
   taskid  = taskidin;
   nx      = nxin;
   ny      = nyin;
   nz      = nzin;
   maptype = mapin;

   /* slab mapping */
   if (maptype==1)
   {
      qmap[0] =  new (std::nothrow) int[nz]();
      pmap[0] =  new (std::nothrow) int[nz]();
      kmap    =  new (std::nothrow) int[nz]();

       /* cyclic mapping */
       p = 0; q = 0;
       for (auto k=0; k<nz; ++k)
       {
          qmap[0][k] = q;
          pmap[0][k] = p;
          if (p==taskid) nq=q+1;
          ++p;
          if (p>=np) {p=0; ++q;}
       }
       nfft3d=(nx/2+1)*ny*nq;
       n2ft3d=2*nfft3d;

   }

   /* hilbert or hcurve  mapping */
   else
   {
      qmap[0] =  new (std::nothrow) int[ny*nz]();       pmap[0] =  new (std::nothrow) int[ny*nz]();
      qmap[1] =  new (std::nothrow) int[nz*(nx/2+1)](); pmap[1] =  new (std::nothrow) int[nz*(nx/2+1)]();
      qmap[2] =  new (std::nothrow) int[(nx/2+1)*ny](); pmap[2] =  new (std::nothrow) int[(nx/2+1)*ny]();

      if (maptype>0) {
         if (maptype==2)
         {
            hilbert2d_map(ny,nz,pmap[0]);
            hilbert2d_map(nz,(nx/2+1),pmap[1]);
            hilbert2d_map((nx/2+1),ny,pmap[2]);
         }
         if (maptype==3)
         {
            hcurve2d_map(ny,nz,pmap[0]);
            hcurve2d_map(nz,(nx/2+1),pmap[1]);
            hcurve2d_map((nx/2+1),ny,pmap[2]);
         }
         nq1 = generate_map_indexes(taskid,np,ny,nz,      pmap[0],qmap[0]);
         nq2 = generate_map_indexes(taskid,np,nz,(nx/2+1),pmap[1],qmap[1]);
         nq3 = generate_map_indexes(taskid,np,(nx/2+1),ny,pmap[2],qmap[2]);

      /* double grid map1 defined wrt to single grid         */
      /* makes expand and contract routines trivial parallel */
      } else {
         int nyh = ny/2;
         int nzh = nz/2;
         int *qmap_h0 =  new (std::nothrow) int[(nyh*nzh)]();        
         int *pmap_h0 =  new (std::nothrow) int[(nyh*nzh)]();

         if (maptype==-2)
         {
            hilbert2d_map(nyh,nzh,    pmap_h0);
            hilbert2d_map(nz,(nx/2+1),pmap[1]);
            hilbert2d_map((nx/2+1),ny,pmap[2]);
         }
         if (maptype==-3)
         {
            hcurve2d_map(nyh,nzh,    pmap_h0);
            hcurve2d_map(nz,(nx/2+1),pmap[1]);
            hcurve2d_map((nx/2+1),ny,pmap[2]);
         }
         nq1 = 4*generate_map_indexes(taskid,np,nyh,nzh,  pmap_h0,qmap_h0);
         nq2 = generate_map_indexes(taskid,np,nz,(nx/2+1),pmap[1],qmap[1]);
         nq3 = generate_map_indexes(taskid,np,(nx/2+1),ny,pmap[2],qmap[2]);
         maptype = -maptype;

         expand_hilbert2d(np,nyh,nzh,pmap_h0,qmap_h0,     pmap[0],qmap[0]);
         delete [] pmap_h0;
         delete [] qmap_h0;
      }
      nfft3d = (nx/2+1)*nq1;
      if ((ny*nq2) > nfft3d) nfft3d=ny*nq2;
      if ((nz*nq3) > nfft3d) nfft3d=nz*nq3;
      n2ft3d = 2*nfft3d;
   }
}


/********************************
 *                              *
 *          Destructors         *
 *                              *
 ********************************/
Mapping3::~Mapping3()
{
   for (int i=0; i<3; ++i)
   {
      if (qmap[i]) delete [] pmap[i];
      if (pmap[i]) delete [] qmap[i];
   }
   if (kmap) delete [] kmap;

   if ((maptype==2) || (maptype==3))
   {
        //int *h_iq_to_i1[6],*h_iq_to_i2[6];
        //int *h_i1_start[6],*h_i2_start[6];
   }


}

}

