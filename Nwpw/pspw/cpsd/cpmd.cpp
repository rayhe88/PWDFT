
#include	<iostream>
#include	<cstdio>
#include	<cmath>
#include	<cstdlib>
#include	<string>



#include	"Parallel.hpp"
//#include	"control.hpp"
#include	"Control2.hpp"
#include	"util_date.hpp"
#include	"Lattice.hpp"
#include	"PGrid.hpp"
#include	"Pneb.hpp"
#include	"Ion.hpp"
#include	"Ewald.hpp"
#include	"Strfac.hpp"
#include	"nwpw_Nose_Hoover.hpp"
#include	"Kinetic.hpp"
#include	"Coulomb.hpp"
#include	"exchange_correlation.hpp"
#include	"Pseudopotential.hpp"
#include	"nwpw_aimd_running_data.hpp"
#include	"inner_loop_md.hpp"
#include	"psi.hpp"
#include	"rtdb.hpp"
#include	"mpi.h"

#include "json.hpp"
using json = nlohmann::json;

#define Efmt(w,p) std::right << std::setw(w) << std::setprecision(p)  << std::scientific
#define Ffmt(w,p) std::right << std::setw(w) << std::setprecision(p)  << std::fixed
#define Ifmt(w)   std::right << std::setw(w) 

namespace pwdft {



/******************************************
 *                                        *
 *                 cpmd                   *
 *                                        *
 ******************************************/
int cpmd(MPI_Comm comm_world0, std::string& rtdbstring)
{
   //Parallel myparallel(argc,argv);
   Parallel myparallel(comm_world0);
   //RTDB myrtdb(&myparallel, "eric.db", "old");

   bool verlet,SA;
   int version,nfft[3],ne[2],ispin;
   int i,ii,ia,nn,ngrid[3],matype,nelem,icount,done;
   char date[26];
   double sum1,sum2,ev;
   double cpu1,cpu2,cpu3,cpu4;
   double E[60],viral,unita[9];
   double eave,evar,have,hvar,qave,qvar,eke;
   double *psi0,*psi1,*psi2,*Hpsi,*psi_r;
   double *dn;
   double *hml,*lmbda,*eig;
   double sa_alpha[2],sa_decay[2],Te_init,Tr_init,Te_new,Tr_new;
   double kb = 3.16679e-6;

   Control2 control(myparallel.np(),rtdbstring);

   bool hprint = (myparallel.is_master() && control.print_level("high"));
   bool oprint = (myparallel.is_master() && control.print_level("medium"));
   bool lprint = (myparallel.is_master() && control.print_level("low"));

   /* reset Parallel base_stdio_print = lprint */
   myparallel.base_stdio_print = lprint;

   for (ii=0; ii<60; ++ii) E[ii] = 0.0;

   if (myparallel.is_master()) seconds(&cpu1);
   if (oprint)
   {
      std::ios_base::sync_with_stdio();
      std::cout << "          *****************************************************\n";
      std::cout << "          *                                                   *\n";
      std::cout << "          *     Car-Parrinello calculation for molecules,     *\n";
      std::cout << "          *       microclusters, liquids, and materials       *\n";
      std::cout << "          *                                                   *\n";
      std::cout << "          *     [     extended Lagrangian molecular   ]       *\n";
      std::cout << "          *     [        dynamics simulation          ]       *\n";
      std::cout << "          *     [          C++ implementation         ]       *\n";
      std::cout << "          *                                                   *\n";
      std::cout << "          *            version #7.00   03/20/20               *\n";
      std::cout << "          *                                                   *\n";
      std::cout << "          *    This code was developed by Eric J. Bylaska     *\n";
      std::cout << "          *                                                   *\n";
      std::cout << "          *****************************************************\n";
      std::cout << "          >>> job started at       " << util_date() << " <<<\n";
   }

   Lattice mylattice(control);
   //myparallel.init2d(control_np_orbital());
   myparallel.init2d(control.np_orbital(),control.pfft3_qsize());

   /* initialize lattice, parallel grid structure */
   psi_get_header(&myparallel,&version,nfft,unita,&ispin,ne,control.input_movecs_filename());
   Pneb mygrid(&myparallel,&mylattice,control,ispin,ne);

   /* initialize psi0, psi1, and psi2 */
   psi0  = mygrid.g_allocate(1);
   psi1  = mygrid.g_allocate(1);
   psi2  = mygrid.g_allocate(1);
   Hpsi  = mygrid.g_allocate(1);
   psi_r = mygrid.h_allocate();
   dn    = mygrid.r_nalloc(ispin);
   hml   = mygrid.m_allocate(-1,1);
   lmbda = mygrid.m_allocate(-1,1);
   eig   = new double[ne[0]+ne[1]];

   /* read wavefunction */
   psi_read0(&mygrid,&version,nfft,unita,&ispin,ne,psi2,control.input_movecs_filename());

   /* ortho check */
   sum2  = mygrid.gg_traceall(psi2,psi2);
   sum1  = ne[0]+ne[1];
   if (ispin==1) sum1 *= 2;
   if (std::fabs(sum2-sum1)>1.0e-10)
   {
      if (oprint)
         std::cout << "Warning: Gram-Schmidt Being performed on psi2" << std::endl;
   }

   /* read wavefunction velocities */
   mygrid.g_zero(psi1);
   if (psi_filefind(&mygrid,control.input_v_movecs_filename()))
      psi_read0(&mygrid,&version,nfft,unita,&ispin,ne,psi1,control.input_v_movecs_filename());


   /* read in ion structure */
   //Ion myion(myrtdb);
   Ion myion(rtdbstring,control);

   /* setup structure factor */
   Strfac mystrfac(&myion, &mygrid);
   mystrfac.phafac();

   /* initialize operators */
   Kinetic_Operator mykin(&mygrid);
   Coulomb_Operator mycoulomb(&mygrid);
   XC_Operator      myxc(&mygrid,control);
   
   Pseudopotential mypsp(&myion,&mygrid,&mystrfac,control);

   /* setup ewald */
   Ewald myewald(&myparallel,&myion,&mylattice,control,mypsp.zv);
   myewald.phafac();

   /* scaling psi velocity */
   mygrid.gg_copy(psi1,psi0);
   mygrid.g_Scale(control.scaling(0),psi1);
   double eke0 = control.fake_mass()*mygrid.gg_traceall(psi0,psi0); 
   double eke1 = control.fake_mass()*mygrid.gg_traceall(psi1,psi1); 

   /* initialize thermostats */
   double w = mykin.ke_ave(psi2);
   nwpw_Nose_Hoover mynose(myion,(mygrid.ne[0]+mygrid.ne[1]),w,control);

   /* initialize simulated annealing */
   SA       = false;
   Te_init  = 0.0;
   Tr_init  = 0.0;
   sa_alpha[0]     = 1.0;
   sa_alpha[1]     = 1.0;
   if (control.SA())
   {
      sa_decay[0] = control.SA_decay(0);
      sa_decay[1] = control.SA_decay(1);
      if (mynose.on()) 
      {
         SA      = true;
         Te_init = mynose.Te;
         Tr_init = mynose.Tr;
      }
      else
      {
         double dt   = control.time_step();
         SA          = false;
         sa_alpha[0] = exp(-(dt/sa_decay[0]));
         sa_alpha[1] = exp(-(dt/sa_decay[1]));
      }
   }


   /* Initialize simulated annealing */

   /* initialize two-electron Gaussian integrals */
   /* initialize paw ncmp*Vloc */

   /* initialize metadynamics and tamd */

   /* initialize dplot */

   /* initialize SIC and HFX */

   /* initialize DFT+U */

   /* initialize META GGA */

   /* initialize vdw */

   /* initialize pressure */





//                 |**************************|
// *****************   summary of input data  **********************
//                 |**************************|

   if (oprint)
   {
      std::cout << "\n\n";
      std::cout << "          ==============  summary of input  ==================\n";
      std::cout << "\n input psi filename:  " << control.input_movecs_filename() << "\n";
      std::cout << " input vpsi filename: " << control.input_v_movecs_filename() << "\n";
      std::cout << "\n";
      std::cout << " number of processors used: " << myparallel.np() << "\n";
      std::cout << " processor grid           : " << myparallel.np_i() << " x" << myparallel.np_j() << "\n";
      if (mygrid.maptype==1) std::cout << " parallel mapping         : 1d-slab"    << "\n";
      if (mygrid.maptype==2) std::cout << " parallel mapping         : 2d-hilbert" << "\n";
      if (mygrid.maptype==3) std::cout << " parallel mapping         : 2d-hcurve" << "\n";
      if (mygrid.isbalanced()) 
         std::cout << " parallel mapping         : balanced" << "\n";
      else
         std::cout << " parallel mapping         : not balanced" << "\n";

      std::cout << "\n options:\n";
      std::cout << "   ion motion           = ";
      std::cout << "yes\n";
      std::cout << "   boundary conditions  = ";
      if (control.version==3) std::cout << "periodic\n";
      if (control.version==4) std::cout << "aperiodic\n";

      std::cout << "   electron spin        = ";
      if (ispin==1)
         std::cout << "restricted\n";
      else
         std::cout << "unrestricted\n";
      std::cout << myxc;
  
      //std::cout << "\n elements involved in the cluster:\n";
      //for (ia=0; ia<myion.nkatm; ++ia)
      //{
      //   printf("    %2d : %4s   core charge: %4.1lf  lmax=%1d\n",
      //           ia+1,myion.atom(ia),mypsp.zv[ia],mypsp.lmax[ia]);
      //   printf("           comment : %s\n",mypsp.comment[ia]);
      //   printf("           pseudopotential type            : %3d\n",mypsp.psp_type[ia]);
      //   printf("           highest angular component       : %3d\n",mypsp.lmax[ia]);
      //   printf("           local potential used            : %3d\n",mypsp.locp[ia]);
      //   printf("           number of non-local projections : %3d\n",mypsp.nprj[ia]);
      //   if (mypsp.semicore[ia])
      //      printf("           semicore corrections included   : %6.3lf (radius) %6.3lf (charge)\n",mypsp.rcore[ia],mypsp.ncore(ia));
      //   printf("           cutoff = ");
      //   for (ii=0; ii<=mypsp.lmax[ia]; ++ii)
      //      printf("%8.3lf",mypsp.rc[ia][ii]);
      //   printf("\n");
      //}
      std::cout << mypsp.print_pspall();

      std::cout << "\n total charge:" << Ffmt(8,3) << control.total_charge() << std::endl;

      std::cout << "\n atom composition:" << std::endl;
      for (ia=0; ia<myion.nkatm; ++ia)
         std::cout << "   " << myion.atom(ia) << " : " << myion.natm[ia];
      std::cout << "\n\n initial ion positions of ions (au):" << std::endl;
      for (ii=0; ii<myion.nion; ++ii)
         std::cout << Ifmt(4) << ii+1 << " " << myion.symbol(ii)
                   << "\t( " << Ffmt(10,5) << myion.rion(0,ii)  << " " << Ffmt(10,5) << myion.rion(1,ii) << " " << Ffmt(10,5) << myion.rion(2,ii)
                   << " ) - atomic mass = " << Ffmt(6,3) << myion.amu(ii) << std::endl;
      std::cout << "   G.C.\t( " << Ffmt(10,5) << myion.gc(0) << " " << Ffmt(10,5) << myion.gc(1) << " " << Ffmt(10,5) << myion.gc(2) << " )" << std::endl;
      std::cout << " C.O.M.\t( " << Ffmt(10,5) << myion.com(0) << " " << Ffmt(10,5) << myion.com(1) << " " << Ffmt(10,5) << myion.com(2) << " )" << std::endl;
      
      std::cout << "\n\n initial velocity of ions (au):" << std::endl;
      for (ii=0; ii<myion.nion; ++ii)
         std::cout << Ifmt(4) << ii+1 << " " << myion.symbol(ii)
                   << "\t( " << Ffmt(10,5) << myion.vion(0,ii) << " " << Ffmt(10,5) << myion.vion(1,ii) << " " << Ffmt(10,5) << myion.vion(2,ii)
                   << " )"   << std::endl;
      std::cout << "   G.C.\t( " << Ffmt(10,5) << myion.vgc(0) << " " << Ffmt(10,5) << myion.vgc(1) << " " << Ffmt(10,5) << myion.vgc(2) << " )" << std::endl;
      std::cout << " C.O.M.\t( " << Ffmt(10,5) << myion.vcom(0) << " " << Ffmt(10,5) << myion.vcom(1) << " " << Ffmt(10,5) << myion.vcom(2) << " )" << std::endl;
      std::cout << " number of constraints = " << Ifmt(5) << 0 << " ( DOF = " << Ifmt(6) << myion.ndof() << " )" << std::endl;
      std::cout << std::endl;

      std::cout <<" number of electrons: spin up=" << Ifmt(6) << mygrid.ne[0] << " (" << Ifmt(4) << mygrid.neq[0]
                << " per task) down=" << Ifmt(6) << mygrid.ne[ispin-1] << " (" << Ifmt(4) << mygrid.neq[ispin-1] << " per task)" << std::endl;


      std::cout << std::endl;
      std::cout << " supercell:" << std::endl;
      std::cout << "      volume = " << Ffmt(10,2) << mylattice.omega() << std::endl;
      std::cout << "      lattice:    a1=< " << Ffmt(8,3) << mylattice.unita(0,0) << " " << Ffmt(8,3) << mylattice.unita(1,0) << " " << Ffmt(8,3) << mylattice.unita(2,0) << " >\n";
      std::cout << "                  a2=< " << Ffmt(8,3) << mylattice.unita(0,1) << " " << Ffmt(8,3) << mylattice.unita(1,1) << " " << Ffmt(8,3) << mylattice.unita(2,1) << " >\n";
      std::cout << "                  a3=< " << Ffmt(8,3) << mylattice.unita(0,2) << " " << Ffmt(8,3) << mylattice.unita(1,2) << " " << Ffmt(8,3) << mylattice.unita(2,2) << " >\n";
      std::cout << "      reciprocal: b1=< " << Ffmt(8,3) << mylattice.unitg(0,0) << " " << Ffmt(8,3) << mylattice.unitg(1,0) << " " << Ffmt(8,3) << mylattice.unitg(2,0) << " >\n";
      std::cout << "                  b2=< " << Ffmt(8,3) << mylattice.unitg(0,1) << " " << Ffmt(8,3) << mylattice.unitg(1,1) << " " << Ffmt(8,3) << mylattice.unitg(2,1) << " >\n";
      std::cout << "                  b3=< " << Ffmt(8,3) << mylattice.unitg(0,2) << " " << Ffmt(8,3) << mylattice.unitg(1,2) << " " << Ffmt(8,3) << mylattice.unitg(2,2) << " >\n";

      {double aa1,bb1,cc1,alpha1,beta1,gamma1;
       mylattice.abc_abg(&aa1,&bb1,&cc1,&alpha1,&beta1,&gamma1);
       std::cout << "      lattice:    a=    " << Ffmt(8,3) << aa1    << " b=   " << Ffmt(8,3) << bb1   << " c=    " << Ffmt(8,3) << cc1 << std::endl;
       std::cout << "                  alpha=" << Ffmt(8,3) << alpha1 << " beta=" << Ffmt(8,3) << beta1 << " gamma=" << Ffmt(8,3) << gamma1<< std::endl;}
      std::cout << "      density cutoff= " << Ffmt(7,3) << mylattice.ecut()
                << " fft= " << Ifmt(4) << mygrid.nx << " x " << Ifmt(4) << mygrid.ny << " x " << Ifmt(4) << mygrid.nz
                << "  (" << Ifmt(8) << mygrid.npack_all(0) << " waves " << Ifmt(8) << mygrid.npack(0) << " per task)" << std::endl;
      std::cout << "      wavefnc cutoff= " << Ffmt(7,3) << mylattice.wcut()
                << " fft= " << Ifmt(4) << mygrid.nx << " x " << Ifmt(4) << mygrid.ny << " x " << Ifmt(4) << mygrid.nz
                << "  (" << Ifmt(8) << mygrid.npack_all(1) << " waves " << Ifmt(8) << mygrid.npack(1) << " per task)" << std::endl;
      std::cout << "\n";
      std::cout << " ewald parameters:\n";
      std::cout << "      energy cutoff = " << Ffmt(7,3) << myewald.ecut()
                << " fft= " << Ifmt(4) << myewald.nx() << " x " << Ifmt(4) << myewald.ny() << " x " << Ifmt(4) << myewald.nz()
                << "  (" << Ifmt(8) << myewald.npack_all() << " waves " << Ifmt(8) << myewald.npack() << " per task)" << std::endl;
      std::cout << "      Ewald summation: cut radius=  " << Ffmt(7,3) << myewald.rcut() << " and " << Ifmt(3) << myewald.ncut() << std::endl;
      std::cout << "                       Mandelung Wigner-Seitz= " << Ffmt(12,8) << myewald.mandelung()
                << " (alpha=" << Ffmt(12,8) << myewald.rsalpha() << " rs=" << Ffmt(11,8) << myewald.rs() << ")" << std::endl;

      std::cout << std::endl;
      std::cout << " technical parameters:" << std::endl;
      if (myion.fix_translation) std::cout << "      translation constrained" << std::endl;
      if (myion.fix_rotation)    std::cout << "      rotation constrained" << std::endl;
      std::cout << "      time step= " << Ffmt(11,2) << control.time_step() << " ficticious mass=" << Ffmt(11,2) << control.fake_mass() << std::endl;
      //printf("      tolerance=%12.3le (energy) %12.3le (density) %12.3le (ion)\n",
      //       control.tolerances(0),control.tolerances(1),control.tolerances(2));

      std::cout << "      max iterations = " << Ifmt(10) << control.loop(0)*control.loop(1) 
                << " (" << Ifmt(5) << control.loop(0) << " inner " << Ifmt(5) << control.loop(1) << " outer)" << std::endl;
      std::cout << std::endl;
      std::cout << " cooling/heating rates:  " << Efmt(12,5) << control.scaling(0) << " (psi) " << Efmt(12,5) << control.scaling(1) << " (ion)" << std::endl;
      std::cout << " initial kinetic energy: " << Efmt(12,5) << eke0 << " (psi) " << Efmt(12,5) << myion.eki0 << " (ion)" << std::endl;
      std::cout << "                                           " << Efmt(12,5) << myion.ekg << " (C.O.M.)" << std::endl;
      std::cout << " after scaling:          " << Efmt(12,5) << eke1 << " (psi) " << Efmt(12,5) << myion.eki1 << " (ion)" << std::endl;
      std::cout << " increased energy:       " << Efmt(12,5) << eke1-eke0 << " (psi) " << Efmt(12,5) << myion.eki1-myion.eki0 << " (ion)" << std::endl;
      std::cout << std::endl;

      if (mynose.on()) 
         std::cout << mynose.inputprint();
      else
         std::cout << " Constant Energy Simulation" << std::endl;

      if (SA) std::cout << "      SA decay rate = " << Efmt(10,3) << sa_decay[0] << "  (elc) " << Efmt(10,3) << sa_decay[1]  << " (ion)" << std::endl;

      std::cout << std::endl << std::endl;
 
   }


//                 |**************************|
// *****************     start iterations     **********************
//                 |**************************|

   if (myparallel.is_master()) seconds(&cpu2);
   if (oprint)
   {
      std::cout << "         ================ Car-Parrinello iteration ================\n";
      std::cout << "     >>> iteration started at " << util_date() << " <<<\n";
      std::cout << "     iter.          KE+Energy             Energy        KE_psi        KE_Ion   Temperature\n";
      std::cout << "     -------------------------------------------------------------------------------------\n";
   }

   // Newton step - first step using velocity
   verlet = false;
   inner_loop_md(verlet,sa_alpha,control,&mygrid,&myion,&mynose,
                 &mykin,&mycoulomb,&myxc,
                 &mypsp,&mystrfac,&myewald,
                 psi0,psi1,psi2,Hpsi,psi_r,
                 dn,hml,lmbda,
                 1,E);


   //Verlet Block: Position Verlet loop  - steps: r2 = 2*r1 - r0 + 0.5*a
   icount = 0;
   if (control.loop(1) > 0)
   {
      // Initialize AIMD running data
      nwpw_aimd_running_data mymotion_data(control,&myparallel,&mygrid,&myion,E,hml,psi1,dn);

      int it_in = control.loop(0);
      verlet = true;
      eke    = 0.0;
      done   = 0;
      while (!done)
      {
         ++icount;
         inner_loop_md(verlet,sa_alpha,control,&mygrid,&myion,&mynose,
                    &mykin,&mycoulomb,&myxc,
                    &mypsp,&mystrfac,&myewald,
                    psi0,psi1,psi2,Hpsi,psi_r,
                    dn,hml,lmbda,
                    it_in,E);
          eke += E[2];

         // Update Metadynamics and TAMD 

         // Write out loop energies
         if (oprint)
         {
            if (SA)
               std::cout << Ifmt(10) << icount*control.loop(0) 
                         << Efmt(19,10) << E[0] << Efmt(19,10) << E[1] 
                         << Efmt(14,5)  << E[2] << Efmt(14,5)  << E[3] 
                         << Ffmt(9,1)   << Te_new << Ffmt(9,1) << Tr_new << std::endl; 
               //printf("%10d%19.10le%19.10le%14.5le%14.5le%9.1lf%9.1lf\n",icount*control.loop(0),
               //                           E[0],E[1],E[2],E[3],Te_new,Tr_new);
            else
               std::cout << Ifmt(10) << icount*control.loop(0) 
                         << Efmt(19,10) << E[0] << Efmt(19,10) << E[1] 
                         << Efmt(14,5)  << E[2] << Efmt(14,5)  << E[3] 
                         << Ffmt(14,2)  << myion.Temperature() << std::endl; 
               //printf("%10d%19.10le%19.10le%14.5le%14.5le%14.2lf\n",icount*control.loop(0),
               //                           E[0],E[1],E[2],E[3],myion.Temperature());
         }


         // Update AIMD Running data
         mymotion_data.update_iteration(icount);
         eave = mymotion_data.eave; evar = mymotion_data.evar;
         have = mymotion_data.have; hvar = mymotion_data.hvar;
         qave = mymotion_data.qave; qvar = mymotion_data.qvar;


         // update thermostats using SA decay


         // Check for running out of time
         if (control.out_of_time())
         {
            done = 1;
            if (oprint) std::cout << "         *** out of time. iteration terminated." << std::endl;
         }
         // Check for Completion
         else if (icount>=control.loop(1))
         {
            done = 1;
            if (oprint) std::cout << "         *** arrived at the Maximum iteration.   terminated." << std::endl;
         }

      } // end while loop

   } // Verlet Block

   if (myparallel.is_master()) seconds(&cpu3);
   if (oprint) 
   {
     std::cout << "     >>> iteration ended at   " << util_date() << " <<<\n";
   }
// *******************  end of iteration loop  ***********************



//          |****************************************|
// ********** post data checking and diagonalize hml *****************
//          |****************************************|

   /* diagonalize the hamiltonian */
   mygrid.m_diagonalize(hml,eig);

   /* rotate current psi and psi0 */
   mygrid.fmf_Multiply(0,psi1,hml,1.0, psi2,0.0);

   mygrid.gg_copy(psi0,psi1);
   mygrid.fmf_Multiply(0,psi1,hml,1.0, psi0,0.0);



//                  |***************************|
// ****************** report summary of results **********************
//                  |***************************|
   if (oprint) 
   {
      util_print_elapsed_time(icount*control.loop(0)*control.time_step());
      std::cout << std::endl << std::endl;
      std::cout << "          =============  summary of results  =================" << std::endl;
      std::cout << "\n final position of ions (au):" << std::endl;
      for (ii=0; ii<myion.nion; ++ii)
         std::cout << Ifmt(4) << ii+1 << " " << myion.symbol(ii)
                   << "\t( " << Ffmt(10,5) << myion.rion(0,ii)  << " " << Ffmt(10,5) << myion.rion(1,ii) << " " << Ffmt(10,5) << myion.rion(2,ii)
                   << " ) - atomic mass = " << Ffmt(6,3) << myion.amu(ii) << std::endl;
      std::cout << "   G.C.\t( " << Ffmt(10,5) << myion.gc(0)  << " " << Ffmt(10,5) << myion.gc(1)  << " " << Ffmt(10,5) << myion.gc(2)  << " )" << std::endl;
      std::cout << " C.O.M.\t( " << Ffmt(10,5) << myion.com(0) << " " << Ffmt(10,5) << myion.com(1) << " " << Ffmt(10,5) << myion.com(2) << " )" << std::endl;
      std::cout << "\n final velocity of ions (au):" << std::endl;
      for (ii=0; ii<myion.nion; ++ii)
         std::cout << Ifmt(4) << ii+1 << " " << myion.symbol(ii)
                   << "\t( " << Ffmt(10,5) << myion.vion(0,ii) << " " << Ffmt(10,5) << myion.vion(1,ii) << " " << Ffmt(10,5) << myion.vion(2,ii)
                   << " )"   << std::endl;
      std::cout << "   G.C.\t( " << Ffmt(10,5) << myion.vgc(0) << " " << Ffmt(10,5) << myion.vgc(1) << " " << Ffmt(10,5) << myion.vgc(2) << " )" << std::endl;
      std::cout << " C.O.M.\t( " << Ffmt(10,5) << myion.vcom(0) << " " << Ffmt(10,5) << myion.vcom(1) << " " << Ffmt(10,5) << myion.vcom(2) << " )" << std::endl;
      std::cout << " number of constraints = " << Ifmt(5) << 0 << " ( DOF = " << Ifmt(6) << myion.ndof() << " )" << std::endl;
      std::cout << std::endl;

      if (mypsp.myapc->v_apc_on) 
         std::cout << mypsp.myapc->shortprint_APC();

      std::cout << " total     energy        : " << Efmt(19,10) << E[1] << " (" << Efmt(15,5) << E[1]/myion.nion << " /ion)" << std::endl;
      std::cout << " total orbital energy    : " << Efmt(19,10) << E[4] << " (" << Efmt(15,5) << E[4]/(mygrid.ne[0]+mygrid.ne[1]) << " /electron)" << std::endl;
      std::cout << " hartree energy          : " << Efmt(19,10) << E[5] << " (" << Efmt(15,5) << E[5]/(mygrid.ne[0]+mygrid.ne[1]) << " /electron)" << std::endl;
      std::cout << " exc-corr energy         : " << Efmt(19,10) << E[6] << " (" << Efmt(15,5) << E[6]/(mygrid.ne[0]+mygrid.ne[1]) << " /electron)" << std::endl;
      if (mypsp.myapc->v_apc_on)
         std::cout << " APC energy              : " << Efmt(19,10) << E[51] << " (" << Efmt(15,5) << E[51]/myion.nion << " /ion)" << std::endl;
      std::cout << " ion-ion energy          : " << Efmt(19,10) << E[7] << " (" << Efmt(15,5) << E[7]/myion.nion << " /ion)" << std::endl << std::endl;

      std::cout << " Kinetic energy    (elc) : " << Efmt(19,10) << E[2] << " (" << Efmt(15,5) << E[2]/(mygrid.ne[0]+mygrid.ne[1]) << " /electron)" << std::endl;
      std::cout << " Kinetic energy    (ion) : " << Efmt(19,10) << E[3] << " (" << Efmt(15,5) << E[3]/myion.nion << " /ion)" << std::endl;
      if (mynose.on())
      {
         // BUG!! the wrong energies need to be fixed!
         std::cout << " thermostat energy (elc) : " << Efmt(19,10) << E[3] << " (" << Efmt(15,5) << E[3]/(mygrid.ne[0]+mygrid.ne[1]) << " /electron)" << std::endl;
         std::cout << " thermostat energy (ion) : " << Efmt(19,10) << E[3] << " (" << Efmt(15,5) << E[3] << myion.nion << " /ion)" << std::endl;
      }

      std::cout << "\n final kinetic energy:   " << Efmt(12,5) << E[2] <<" (psi) " << Efmt(12,5) << E[3] << " (ion)" << std::endl;
      std::cout << "                                            " << Efmt(12,5) << myion.ekg << " (C.O.M.)" << std::endl;
      eke /= ((double) control.loop(1));
      eke *= 2.0 / kb / ((double) (mygrid.ne[0]+mygrid.ne[ispin-1])) / ((double) mygrid.npack_all(1));
      std::cout << " Temperature         :   " << Ffmt(10,1) << eke << " K (elc)" << std::endl;
      std::cout << " Temperature         :   " << Ffmt(10,1) << myion.Temperature() << " K (ion)" << std::endl;
      std::cout << "                     :   " << Ffmt(10,1) << myion.com_Temperature() << " K (C.O.M.)" << std::endl << std::endl; 

      std::cout << " Vaverge   Eaverage  :   " << Efmt(19,10) << eave << " " << Efmt(19,10) << have << std::endl; 
      std::cout << " Vvariance Evariance :   " << Efmt(19,10) << evar << " " << Efmt(19,10) << hvar << std::endl;
      double cv = myion.Temperature();
      cv = (evar)/(kb*cv*cv);
      cv /= ((double) myion.nion);
      std::cout << " Cv - f*kb/(2*nion)  :   " << Efmt(19,10) << cv << std::endl;

      //printf(" K.S. kinetic energy : %19.10le (%15.5le /electron)\n",      E[5],E[5]/(mygrid.ne[0]+mygrid.ne[1]));
      //printf(" K.S. V_l energy     : %19.10le (%15.5le /electron)\n",      E[6],E[6]/(mygrid.ne[0]+mygrid.ne[1]));
      //printf(" K.S. V_nl energy    : %19.10le (%15.5le /electron)\n",      E[7],E[7]/(mygrid.ne[0]+mygrid.ne[1]));
      //printf(" K.S. V_Hart energy  : %19.10le (%15.5le /electron)\n",      E[8],E[8]/(mygrid.ne[0]+mygrid.ne[1]));
      //printf(" K.S. V_xc energy    : %19.10le (%15.5le /electron)\n",      E[9],E[9]/(mygrid.ne[0]+mygrid.ne[1]));
      //if (mypsp.myapc->v_apc_on)
      //   printf(" K.S. V_APC energy   : %19.10le (%15.5le /ion)\n",      E[52],E[52]/myion.nion);
      //viral = (E[9]+E[8]+E[7]+E[6])/E[5];
      //printf(" Viral Coefficient   : %19.10le\n",viral);

      std::cout << "\n orbital energies:" << std::endl;
      nn = ne[0] - ne[1];
      ev = 27.2116;
      for (i=0; i<nn; ++i)
      {
         std::cout << Efmt(18,7) << eig[i] << " (" << Ffmt(8,3) << eig[i]*ev << "eV)" << std::endl;
      }
      for (i=0; i<ne[1]; ++i)
      {
         std::cout << Efmt(18,7) << eig[i+nn] << eig[i]    << " (" << Ffmt(8,3) << eig[i+nn]*ev << eig[i]*ev << "eV) "
                   << Efmt(18,7) << eig[i+(ispin-1)*ne[0]] << " (" << Ffmt(8,3) << eig[i+(ispin-1)*ne[0]]*ev << "eV)"  << std::endl;
         //printf("%18.7le",eig[i+nn]); printf(" ("); printf("%8.3lf",eig[i+nn]*ev); printf("eV) ");
         //printf("%18.7le",eig[i+(ispin-1)*ne[0]]); printf(" ("); printf("%8.3lf",eig[i+(ispin-1)*ne[0]]*ev); printf("eV)\n");
      }
      std::cout << std::endl << std::endl;
      //std::cout << "\n output psi filename:  " << control.output_movecs_filename() << "\n";
      //std::cout << " output vpsi filename: " << control.output_v_movecs_filename() << "\n";
   }


//                  |***************************|
// ******************         prologue          **********************
//                  |***************************|

   /* write wavefunction and velocity wavefunction */
   psi_write(&mygrid,&version,nfft,unita,&ispin,ne,psi2,control.output_movecs_filename());
   psi_write(&mygrid,&version,nfft,unita,&ispin,ne,psi0,control.output_v_movecs_filename());

   /* deallocate memory */
   mygrid.g_deallocate(psi0);
   mygrid.g_deallocate(psi1);
   mygrid.g_deallocate(psi2);
   mygrid.g_deallocate(Hpsi);
   mygrid.h_deallocate(psi_r);
   mygrid.r_dealloc(dn);
   mygrid.m_deallocate(hml);
   mygrid.m_deallocate(lmbda);
   delete [] eig;


   // write results to the json
   auto rtdbjson =  json::parse(rtdbstring);
   rtdbjson["pspw"]["energy"]   = E[0];
   rtdbjson["pspw"]["energies"] = E;
   if (mypsp.myapc->v_apc_on)
   {
      double qion[myion.nion];
      for (auto ii=0; ii<myion.nion; ++ii)
         qion[ii] = -mypsp.myapc->Qtot_APC(ii) + mypsp.zv[myion.katm[ii]];
      rtdbjson["nwpw"]["apc"]["q"] = std::vector<double>(qion,&qion[myion.nion]);
   }

   // set rtdbjson initialize_wavefunction option to false
   if (rtdbjson["nwpw"]["initialize_wavefunction"].is_boolean()) rtdbjson["nwpw"]["initialize_wavefunction"] = false;

   rtdbstring    = rtdbjson.dump();
   myion.writejsonstr(rtdbstring);

//                 |**************************|
// *****************   report consumed time   **********************
//                 |**************************|
   if (myparallel.is_master()) seconds(&cpu4);
   if (oprint) 
   {
      double t1 = cpu2-cpu1;
      double t2 = cpu3-cpu2;
      double t3 = cpu4-cpu3;
      double t4 = cpu4-cpu1;
      double av = t2/((double ) control.loop(0)*icount);
      //cout.setf(ios::scientific);
      std::cout << std::scientific;
      std::cout << "\n";
      std::cout << " -----------------"    << "\n";
      std::cout << " cputime in seconds"   << "\n";
      std::cout << " prologue    : " << t1 << "\n";
      std::cout << " main loop   : " << t2 << "\n";
      std::cout << " epilogue    : " << t3 << "\n";
      std::cout << " total       : " << t4 << "\n";
      std::cout << " cputime/step: " << av << "\n";
      std::cout << "\n";
      std::cout << " >>> job completed at     " << util_date() << " <<<\n";

   }

   MPI_Barrier(comm_world0);
   return 0;
}
   
}
