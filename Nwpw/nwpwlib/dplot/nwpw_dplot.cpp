
#include <fstream>

#include "iofmt.hpp"
#include	"Control2.hpp"
#include	"nwpw_dplot.hpp"

namespace pwdft {


/* Constructor */ 

/*************************************
 *                                   *
 *      nwpw_dplot::nwpw_dplot       *
 *                                   *
 *************************************/
nwpw_dplot::nwpw_dplot(Ion *myion0, Pneb *mypneb0, Control2& control)
{
   mypneb   = mypneb0;
   myion    = myion0;

   ispin  = mypneb->ispin;
   n2ft3d = mypneb->n2ft3d;
   ne    = mypneb->ne;

   double omega = mypneb->lattice->omega();
   double scal1 = 1.0/((double) ((mypneb->nx)*(mypneb->ny)*(mypneb->nz)));
   dv = omega*scal1;

   permanent_dir_str = control.permanent_dir_str;

   position_tolerance = control.position_tolerance_cubefiles();
   ncell[0] = control.ncell_cubefiles(0);
   ncell[1] = control.ncell_cubefiles(1);
   ncell[2] = control.ncell_cubefiles(2);
   origin[0] = control.origin_cubefiles(0);
   origin[1] = control.origin_cubefiles(1);
   origin[2] = control.origin_cubefiles(2);

}

/* Object functions */

/*************************************
 *                                   *
 *      nwpw_dplot::gcube_write      *
 *                                   *
 *************************************/
void nwpw_dplot::gcube_write(std::string cfilename, const int number, std::string comment, double *rho)
{
   bool is_master = mypneb->d3db::parall->is_master();

   std::string cube_filename = permanent_dir_str + "/" + cfilename;
   std::string cube_string   = mypneb->r_formatwrite_reverse(rho);  

   mypneb->d3db::parall->Barrier();

   double np1 = ((double) mypneb->nx);
   double np2 = ((double) mypneb->ny);
   double np3 = ((double) mypneb->nz);

   double ua[9];
   ua[0] = mypneb->lattice->unita(0,0)/np1;
   ua[1] = mypneb->lattice->unita(1,0)/np1;
   ua[2] = mypneb->lattice->unita(2,0)/np1;

   ua[3] = mypneb->lattice->unita(0,1)/np2;
   ua[4] = mypneb->lattice->unita(1,1)/np2;
   ua[5] = mypneb->lattice->unita(2,1)/np2;

   ua[6] = mypneb->lattice->unita(0,2)/np3;
   ua[7] = mypneb->lattice->unita(1,2)/np3;
   ua[8] = mypneb->lattice->unita(2,2)/np3;

   double r0[3];
   r0[0] = -( mypneb->lattice->unita(0,0) + mypneb->lattice->unita(0,1) + mypneb->lattice->unita(0,2) )/2.0;
   r0[1] = -( mypneb->lattice->unita(1,0) + mypneb->lattice->unita(1,1) + mypneb->lattice->unita(1,2) )/2.0;
   r0[2] = -( mypneb->lattice->unita(2,0) + mypneb->lattice->unita(2,1) + mypneb->lattice->unita(2,2) )/2.0;


   int nion2 = myion->nion;

   if (is_master)
   {
      std::ofstream cube_stream(cube_filename);

      // write lattice
      int orb_flag = 1;
      if (number>0) orb_flag=-1;
      cube_stream << "molecule" << std::endl 
                  << comment    << std::endl;
      cube_stream << Ifmt(5) << orb_flag*nion2 << Ffmt(12,6) << r0[0] << Ffmt(12,6) << r0[1] << Ffmt(12,6) << r0[2] << std::endl;
      cube_stream << Ifmt(5) << mypneb->nx     << Ffmt(12,6) << ua[0] << Ffmt(12,6) << ua[3] << Ffmt(12,6) << ua[6] << std::endl;
      cube_stream << Ifmt(5) << mypneb->ny     << Ffmt(12,6) << ua[1] << Ffmt(12,6) << ua[4] << Ffmt(12,6) << ua[7] << std::endl;
      cube_stream << Ifmt(5) << mypneb->nz     << Ffmt(12,6) << ua[2] << Ffmt(12,6) << ua[5] << Ffmt(12,6) << ua[8] << std::endl;

      // write geometry
      for (auto ii=0; ii<myion->nion; ++ii)
      {
          double q  = myion->charge[ii];
          double rx = myion->rion(0,ii);
          double ry = myion->rion(1,ii);
          double rz = myion->rion(2,ii);
          cube_stream << Ifmt(5)    << static_cast<int>(std::round(q)) 
                      << Ffmt(12,6) << q 
                      << Ffmt(12,6) << rx
                      << Ffmt(12,6) << ry
                      << Ffmt(12,6) << rz << std::endl;
      }

      // write orbital header
      if (number>0)
          cube_stream << 1 << " "  << number << std::endl;

      // write orbital grid
      cube_stream << cube_string;
   }

   mypneb->d3db::parall->Barrier();
}

/*************************************
 *                                   *
 *      nwpw_dplot::gcube_write3d    *
 *                                   *
 *************************************/

/*************************************
 *                                   *
 *      nwpw_dplot::gcube_write1d    *
 *                                   *
 *************************************/



}
