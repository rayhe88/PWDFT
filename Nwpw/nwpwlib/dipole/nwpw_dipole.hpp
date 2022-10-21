#ifndef _NWPW_DIPOLE_HPP_
#define _NWPW_DIPOLE_HPP_

// ********************************************************************
// *                                                                  *
// *       nwpw_dipole: used to generate aperiodic and periodic       *
// *                  dipoles from a plane-wave density.              *
// *                                                                  *
// *   The algorithms used in this module are based on the work of    *
// *                                                                  *
// *                                                                  *
// ********************************************************************
#include        <iostream>
#include        "blas.h"

#include        "gdevice.hpp"
#include        "Control2.hpp"
#include        "Ion.hpp"
#include        "Pneb.hpp"
#include        "Strfac.hpp"


namespace pwdft {


class nwpw_dipole {

   Pneb   *mypneb;
   Ion    *myion;
   Strfac *mystrfac;
   double dv;

public:

   bool dipole_on;


   /* constructor */
   nwpw_dipole(Ion *, Pneb *, Strfac *, Control2&);

   /* destructor */
   ~nwpw_dipole() {
   }

   void gen_dipole(const double *, double *);
   //void gen_molecular_dipole(double *, double *);
   //void gen_aperiodic_dipole(double *, double *);
   //void gen_periodic_dipole(double *, double *);

   //double Qtot_APC(const int);
   //std::string shortprint_APC();
   //std::string print_APC(const double *);


};

}

#endif


