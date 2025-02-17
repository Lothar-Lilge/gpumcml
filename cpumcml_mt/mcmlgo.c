/***********************************************************
 *  Copyright Univ. of Texas M.D. Anderson Cancer Center
 *  1992.
 *
 *	Launch, move, and record photon weight.
 ****/

#include "mcml.h"
#include "mwc_prng.h"
#include "dSFMT.h"

#define USE_MT_PRNG 1

//#define NO_ABS 1  //Equivalent to -A flag in GPU-MCML

#define STANDARDTEST 0
  /* testing program using fixed rnd seed. */

#define PARTIALREFLECTION 0     
  /* 1=split photon, 0=statistical reflection. */

#define COSZERO (1.0-1.0E-12)	
  /* cosine of about 1e-6 rad. */

#define COS90D  1.0E-6		
  /* cosine of about 1.57 - 1e-6 rad. */


/***********************************************************
 *	A random number generator from Numerical Recipes in C.
 ****/
#define MBIG 1000000000
#define MSEED 161803398
#define MZ 0
#define FAC 1.0E-9

float ran3(int *idum)
{
  static int inext,inextp;
  static long ma[56];
  static int iff=0;
  long mj,mk;
  int i,ii,k;
  
  if (*idum < 0 || iff == 0) {
    iff=1;
    mj=MSEED-(*idum < 0 ? -*idum : *idum);
    mj %= MBIG;
    ma[55]=mj;
    mk=1;
    for (i=1;i<=54;i++) {
      ii=(21*i) % 55;
      ma[ii]=mk;
      mk=mj-mk;
      if (mk < MZ) mk += MBIG;
      mj=ma[ii];
    }
    for (k=1;k<=4;k++)
      for (i=1;i<=55;i++) {
	ma[i] -= ma[1+(i+30) % 55];
	if (ma[i] < MZ) ma[i] += MBIG;
      }
    inext=0;
    inextp=31;
    *idum=1;
  }
  if (++inext == 56) inext=1;
  if (++inextp == 56) inextp=1;
  mj=ma[inext]-ma[inextp];
  if (mj < MZ) mj += MBIG;
  ma[inext]=mj;
  return mj*FAC;
}

#undef MBIG
#undef MSEED
#undef MZ
#undef FAC


/***********************************************************
 *	Generate a random number between 0 and 1.  Take a 
 *	number as seed the first time entering the function.  
 *	The seed is limited to 1<<15.  
 *	We found that when idum is too large, ran3 may return 
 *	numbers beyond 0 and 1.
 ****/
double RandomNum(void)
{

  static Boolean first_time=1;

  #ifdef USE_MT_PRNG
	static dsfmt_t dsfmt;
	
	if(first_time)
	{
    	first_time = 0;
	    uint32_t seed = time(NULL);
	
	    dsfmt_init_gen_rand(&dsfmt, seed);
	    
	    int i;
	    printf("Starting MT PRNG with seed: %d \n",seed);
	    for(i=0;i<10;i++)
	    {
	    	printf("%f \n",dsfmt_genrand_close_open(&dsfmt));	
	    } 
  	}
  	return dsfmt_genrand_close_open(&dsfmt);
  #else //use the MWC PRNG
	static unsigned long long x;
	if(first_time)
	{
		first_time = 0;
    	x = time(NULL);
	    int i;
	    printf("Starting MWC PRNG with seed: %llu \n",x);
	  	for(i=0;i<10;i++)
		{
			printf("%f \n",(double)rand_MWC_co(&x));	
		}
	} 
	return( (double)rand_MWC_co(&x) ); 
  #endif
	
}

/***********************************************************
 *	Compute the specular reflection. 
 *
 *	If the first layer is a turbid medium, use the Fresnel
 *	reflection from the boundary of the first layer as the 
 *	specular reflectance.
 *
 *	If the first layer is glass, multiple reflections in
 *	the first layer is considered to get the specular
 *	reflectance.
 *
 *	The subroutine assumes the Layerspecs array is correctly 
 *	initialized.
 ****/
double Rspecular(LayerStruct * Layerspecs_Ptr)
{
  double r1, r2;
	/* direct reflections from the 1st and 2nd layers. */
  double temp;
  
  temp =(Layerspecs_Ptr[0].n - Layerspecs_Ptr[1].n)
	   /(Layerspecs_Ptr[0].n + Layerspecs_Ptr[1].n);
  r1 = temp*temp;
  
  if((Layerspecs_Ptr[1].mua == 0.0) 
  && (Layerspecs_Ptr[1].mus == 0.0))  { /* glass layer. */
    temp = (Layerspecs_Ptr[1].n - Layerspecs_Ptr[2].n)
		  /(Layerspecs_Ptr[1].n + Layerspecs_Ptr[2].n);
    r2 = temp*temp;
    r1 = r1 + (1-r1)*(1-r1)*r2/(1-r1*r2);
  }
  
  return (r1);	
}

/***********************************************************
 *	Initialize a photon packet.
 ****/
void LaunchPhoton(double Rspecular,
				  LayerStruct  * Layerspecs_Ptr,
				  PhotonStruct * Photon_Ptr)
{
  Photon_Ptr->w	 	= 1.0 - Rspecular;	
  Photon_Ptr->dead 	= 0;
  Photon_Ptr->layer = 1;
  Photon_Ptr->s	= 0;
  Photon_Ptr->sleft= 0;
  
  Photon_Ptr->x 	= 0.0;	
  Photon_Ptr->y	 	= 0.0;	
  Photon_Ptr->z	 	= 0.0;	
  Photon_Ptr->ux	= 0.0;	
  Photon_Ptr->uy	= 0.0;	
  Photon_Ptr->uz	= 1.0;	
  
  if((Layerspecs_Ptr[1].mua == 0.0) 
  && (Layerspecs_Ptr[1].mus == 0.0))  { /* glass layer. */
    Photon_Ptr->layer 	= 2;
    Photon_Ptr->z	= Layerspecs_Ptr[2].z0;	
  }
}

/***********************************************************
 *	Choose (sample) a new theta angle for photon propagation
 *	according to the anisotropy.
 *
 *	If anisotropy g is 0, then
 *		cos(theta) = 2*rand-1.
 *	otherwise
 *		sample according to the Henyey-Greenstein function.
 *
 *	Returns the cosine of the polar deflection angle theta.
 ****/
double SpinTheta(double g)
{
  double cost;
  
  if(g == 0.0) 
    cost = 2*RandomNum() -1;
  else {
    double temp = (1-g*g)/(1-g+2*g*RandomNum());
    cost = (1+g*g - temp*temp)/(2*g);
	if(cost < -1) cost = -1;
	else if(cost > 1) cost = 1;
  }
  return(cost);
}


/***********************************************************
 *	Choose a new direction for photon propagation by 
 *	sampling the polar deflection angle theta and the 
 *	azimuthal angle psi.
 *
 *	Note:
 *  	theta: 0 - pi so sin(theta) is always positive 
 *  	feel free to use sqrt() for cos(theta).
 * 
 *  	psi:   0 - 2pi 
 *  	for 0-pi  sin(psi) is + 
 *  	for pi-2pi sin(psi) is - 
 ****/
void Spin(double g,
		  PhotonStruct * Photon_Ptr)
{
  double cost, sint;	/* cosine and sine of the */
						/* polar deflection angle theta. */
  double cosp, sinp;	/* cosine and sine of the */
						/* azimuthal angle psi. */
  double ux = Photon_Ptr->ux;
  double uy = Photon_Ptr->uy;
  double uz = Photon_Ptr->uz;
  double psi;

  cost = SpinTheta(g);
  sint = sqrt(1.0 - cost*cost);	
	/* sqrt() is faster than sin(). */

  psi = 2.0*PI*RandomNum(); /* spin psi 0-2pi. */
  cosp = cos(psi);
  if(psi<PI)
    sinp = sqrt(1.0 - cosp*cosp);	
	  /* sqrt() is faster than sin(). */
  else
    sinp = - sqrt(1.0 - cosp*cosp);	
  
  if(fabs(uz) > COSZERO)  { 	/* normal incident. */
    Photon_Ptr->ux = sint*cosp;
    Photon_Ptr->uy = sint*sinp;
    Photon_Ptr->uz = cost*SIGN(uz);	
	  /* SIGN() is faster than division. */
  }
  else  {		/* regular incident. */
    double temp = sqrt(1.0 - uz*uz);
    Photon_Ptr->ux = sint*(ux*uz*cosp - uy*sinp)
					/temp + ux*cost;
    Photon_Ptr->uy = sint*(uy*uz*cosp + ux*sinp)
					/temp + uy*cost;
    Photon_Ptr->uz = -sint*cosp*temp + uz*cost;
  }
}

/***********************************************************
 *	Move the photon s away in the current layer of medium.  
 ****/
void Hop(PhotonStruct *	Photon_Ptr)
{
  double s = Photon_Ptr->s;

  Photon_Ptr->x += s*Photon_Ptr->ux;
  Photon_Ptr->y += s*Photon_Ptr->uy;
  Photon_Ptr->z += s*Photon_Ptr->uz;
}			

/***********************************************************
 *	If uz != 0, return the photon step size in glass, 
 *	Otherwise, return 0.
 *
 *	The step size is the distance between the current 
 *	position and the boundary in the photon direction.
 *
 *	Make sure uz !=0 before calling this function.
 ****/
void StepSizeInGlass(PhotonStruct *  Photon_Ptr,
					 InputStruct  *  In_Ptr)
{
  double dl_b;	/* step size to boundary. */
  short  layer = Photon_Ptr->layer;
  double uz = Photon_Ptr->uz;
  
  /* Stepsize to the boundary. */	
  if(uz>0.0)
    dl_b = (In_Ptr->layerspecs[layer].z1 - Photon_Ptr->z)
		   /uz;
  else if(uz<0.0)
    dl_b = (In_Ptr->layerspecs[layer].z0 - Photon_Ptr->z)
		   /uz;
  else
    dl_b = 0.0;
  
  Photon_Ptr->s = dl_b;
}

/***********************************************************
 *	Pick a step size for a photon packet when it is in 
 *	tissue.
 *	If the member sleft is zero, make a new step size 
 *	with: -log(rnd)/(mua+mus).
 *	Otherwise, pick up the leftover in sleft.
 *
 *	Layer is the index to layer.
 *	In_Ptr is the input parameters.
 ****/
void StepSizeInTissue(PhotonStruct * Photon_Ptr,
					  InputStruct  * In_Ptr)
{
  short  layer = Photon_Ptr->layer;
  double mua = In_Ptr->layerspecs[layer].mua;
  double mus = In_Ptr->layerspecs[layer].mus;
  
  if(Photon_Ptr->sleft == 0.0) {  /* make a new step. */
    double rnd;

    do rnd = RandomNum(); 
      while( rnd <= 0.0 );    /* avoid zero. */
	Photon_Ptr->s = -log(rnd)/(mua+mus);
  }
  else {	/* take the leftover. */
	Photon_Ptr->s = Photon_Ptr->sleft/(mua+mus);
	Photon_Ptr->sleft = 0.0;
  }
}

/***********************************************************
 *	Check if the step will hit the boundary.
 *	Return 1 if hit boundary.
 *	Return 0 otherwise.
 *
 * 	If the projected step hits the boundary, the members
 *	s and sleft of Photon_Ptr are updated.
 ****/
Boolean HitBoundary(PhotonStruct *  Photon_Ptr,
					InputStruct  *  In_Ptr)
{
  double dl_b;  /* length to boundary. */
  short  layer = Photon_Ptr->layer;
  double uz = Photon_Ptr->uz;
  Boolean hit;
  
  /* Distance to the boundary. */
  if(uz>0.0)
    dl_b = (In_Ptr->layerspecs[layer].z1 
			- Photon_Ptr->z)/uz;	/* dl_b>0. */
  else if(uz<0.0)
    dl_b = (In_Ptr->layerspecs[layer].z0 
			- Photon_Ptr->z)/uz;	/* dl_b>0. */
  
  if(uz != 0.0 && Photon_Ptr->s > dl_b) {
	  /* not horizontal & crossing. */
    double mut = In_Ptr->layerspecs[layer].mua 
				+ In_Ptr->layerspecs[layer].mus;

    Photon_Ptr->sleft = (Photon_Ptr->s - dl_b)*mut;
    Photon_Ptr->s    = dl_b;
    hit = 1;
  }
  else
    hit = 0;
  
  return(hit);
}

/***********************************************************
 *	Drop photon weight inside the tissue (not glass).
 *
 *  The photon is assumed not dead. 
 *
 *	The weight drop is dw = w*mua/(mua+mus).
 *
 *	The dropped weight is assigned to the absorption array 
 *	elements.
 ****/
void DropNoAbs(InputStruct  *	In_Ptr, 
		  PhotonStruct *	Photon_Ptr,
		  OutStruct *		Out_Ptr)
{
  double dwa;		/* absorbed weight.*/
  //double x = Photon_Ptr->x;
  //double y = Photon_Ptr->y;
  //double izd, ird;	/* LW 5/20/98. To avoid out of short range.*/
  ///short  iz, ir;	/* index to z & r. */
  short  layer = Photon_Ptr->layer;
  double mua, mus;		
  
  /* compute array indices. */
  //izd = Photon_Ptr->z/In_Ptr->dz;
  //if(izd>In_Ptr->nz-1) iz=In_Ptr->nz-1;
  //else iz = izd;
  
  //ird = sqrt(x*x+y*y)/In_Ptr->dr;
  //if(ird>In_Ptr->nr-1) ir=In_Ptr->nr-1;
  //else ir = ird;
  
  /* update photon weight. */
  mua = In_Ptr->layerspecs[layer].mua;
  mus = In_Ptr->layerspecs[layer].mus;
  dwa = Photon_Ptr->w * mua/(mua+mus);
  Photon_Ptr->w -= dwa;
  
  /* assign dwa to the absorption array element. */
  //Out_Ptr->A_rz[ir][iz]	+= dwa;
}

/***********************************************************
 *	Drop photon weight inside the tissue (not glass).
 *
 *  The photon is assumed not dead. 
 *
 *	The weight drop is dw = w*mua/(mua+mus).
 *
 *	The dropped weight is assigned to the absorption array 
 *	elements.
 ****/
void Drop(InputStruct  *	In_Ptr, 
		  PhotonStruct *	Photon_Ptr,
		  OutStruct *		Out_Ptr)
{
  double dwa;		/* absorbed weight.*/
  double x = Photon_Ptr->x;
  double y = Photon_Ptr->y;
  double izd, ird;	/* LW 5/20/98. To avoid out of short range.*/
  short  iz, ir;	/* index to z & r. */
  short  layer = Photon_Ptr->layer;
  double mua, mus;		
  
  /* compute array indices. */
  izd = Photon_Ptr->z/In_Ptr->dz;
  if(izd>In_Ptr->nz-1) iz=In_Ptr->nz-1;
  else iz = izd;

  ird = sqrt(x*x+y*y)/In_Ptr->dr;
  if(ird>In_Ptr->nr-1) ir=In_Ptr->nr-1;
  else ir = ird;
  
  /* update photon weight. */
  mua = In_Ptr->layerspecs[layer].mua;
  mus = In_Ptr->layerspecs[layer].mus;
  dwa = Photon_Ptr->w * mua/(mua+mus);
  Photon_Ptr->w -= dwa;
  
  /* assign dwa to the absorption array element. */
  Out_Ptr->A_rz[ir][iz]	+= dwa;
}

/***********************************************************
 *	The photon weight is small, and the photon packet tries 
 *	to survive a roulette.
 ****/
void Roulette(PhotonStruct * Photon_Ptr)
{
  if(Photon_Ptr->w == 0.0)	
    Photon_Ptr->dead = 1;
  else if(RandomNum() < CHANCE) /* survived the roulette.*/
    Photon_Ptr->w /= CHANCE;
  else 
    Photon_Ptr->dead = 1;
}

/***********************************************************
 *	Compute the Fresnel reflectance.
 *
 *	Make sure that the cosine of the incident angle a1
 *	is positive, and the case when the angle is greater 
 *	than the critical angle is ruled out.
 *
 * 	Avoid trigonometric function operations as much as
 *	possible, because they are computation-intensive.
 ****/
double RFresnel(double n1,	/* incident refractive index.*/
				double n2,	/* transmit refractive index.*/
				double ca1,	/* cosine of the incident */
							/* angle. 0<a1<90 degrees. */
				double * ca2_Ptr)  /* pointer to the */
							/* cosine of the transmission */
							/* angle. a2>0. */
{
  double r;
  
  if(n1==n2) {			  	/** matched boundary. **/
    *ca2_Ptr = ca1;
    r = 0.0;
  }
  else if(ca1>COSZERO) {	/** normal incident. **/
    *ca2_Ptr = ca1;
    r = (n2-n1)/(n2+n1);
    r *= r;
  }
  else if(ca1<COS90D)  {	/** very slant. **/
    *ca2_Ptr = 0.0;
    r = 1.0;
  }
  else  {			  		/** general. **/
    double sa1, sa2;	
	  /* sine of the incident and transmission angles. */
    double ca2;
    
    sa1 = sqrt(1-ca1*ca1);
    sa2 = n1*sa1/n2;
    if(sa2>=1.0) {	
	  /* double check for total internal reflection. */
      *ca2_Ptr = 0.0;
      r = 1.0;
    }
    else  {
      double cap, cam;	/* cosines of the sum ap or */
						/* difference am of the two */
						/* angles. ap = a1+a2 */
						/* am = a1 - a2. */
      double sap, sam;	/* sines. */
      
      *ca2_Ptr = ca2 = sqrt(1-sa2*sa2);
      
      cap = ca1*ca2 - sa1*sa2; /* c+ = cc - ss. */
      cam = ca1*ca2 + sa1*sa2; /* c- = cc + ss. */
      sap = sa1*ca2 + ca1*sa2; /* s+ = sc + cs. */
      sam = sa1*ca2 - ca1*sa2; /* s- = sc - cs. */
      r = 0.5*sam*sam*(cam*cam+cap*cap)/(sap*sap*cam*cam); 
		/* rearranged for speed. */
    }
  }
  return(r);
}

/***********************************************************
 *	Record the photon weight exiting the first layer(uz<0), 
 *	no matter whether the layer is glass or not, to the 
 *	reflection array.
 *
 *	Update the photon weight as well.
 ****/
void RecordR(double			Refl,	/* reflectance. */
			 InputStruct  *	In_Ptr,
			 PhotonStruct *	Photon_Ptr,
			 OutStruct *	Out_Ptr)
{
  double x = Photon_Ptr->x;
  double y = Photon_Ptr->y;
  short  ir, ia;	/* index to r & angle. */
  double ird, iad;	/* LW 5/20/98. To avoid out of short range.*/
  
  ird = sqrt(x*x+y*y)/In_Ptr->dr;
  if(ird>In_Ptr->nr-1) ir=In_Ptr->nr-1;
  else ir = ird;
  
  iad = acos(-Photon_Ptr->uz)/In_Ptr->da;
  if(iad>In_Ptr->na-1) ia=In_Ptr->na-1;
  else ia = iad;
  
  /* assign photon to the reflection array element. */
  Out_Ptr->Rd_ra[ir][ia] += Photon_Ptr->w*(1.0-Refl);
  
  Photon_Ptr->w *= Refl;
}

/***********************************************************
 *	Record the photon weight exiting the last layer(uz>0), 
 *	no matter whether the layer is glass or not, to the 
 *	transmittance array.
 *
 *	Update the photon weight as well.
 ****/
void RecordT(double 		Refl,
			 InputStruct  *	In_Ptr,
			 PhotonStruct *	Photon_Ptr,
			 OutStruct *	Out_Ptr)
{
  double x = Photon_Ptr->x;
  double y = Photon_Ptr->y;
  short  ir, ia;	/* index to r & angle. */
  double ird, iad;	/* LW 5/20/98. To avoid out of short range.*/
  
  ird = sqrt(x*x+y*y)/In_Ptr->dr;
  if(ird>In_Ptr->nr-1) ir=In_Ptr->nr-1;
  else ir = ird;
  
  iad = acos(Photon_Ptr->uz)/In_Ptr->da; /* LW 1/12/2000. Removed -. */
  if(iad>In_Ptr->na-1) ia=In_Ptr->na-1;
  else ia = iad;
  
  /* assign photon to the transmittance array element. */
  Out_Ptr->Tt_ra[ir][ia] += Photon_Ptr->w*(1.0-Refl);
  
  Photon_Ptr->w *= Refl;
}

/***********************************************************
 *	Decide whether the photon will be transmitted or 
 *	reflected on the upper boundary (uz<0) of the current 
 *	layer.
 *
 *	If "layer" is the first layer, the photon packet will 
 *	be partially transmitted and partially reflected if 
 *	PARTIALREFLECTION is set to 1,
 *	or the photon packet will be either transmitted or 
 *	reflected determined statistically if PARTIALREFLECTION 
 *	is set to 0.
 *
 *	Record the transmitted photon weight as reflection.  
 *
 *	If the "layer" is not the first layer and the photon 
 *	packet is transmitted, move the photon to "layer-1".
 *
 *	Update the photon parmameters.
 ****/
void CrossUpOrNot(InputStruct  *	In_Ptr, 
				  PhotonStruct *	Photon_Ptr,
				  OutStruct *		Out_Ptr)
{
  double uz = Photon_Ptr->uz; /* z directional cosine. */
  double uz1;	/* cosines of transmission alpha. always */
				/* positive. */
  double r=0.0;	/* reflectance */
  short  layer = Photon_Ptr->layer;
  double ni = In_Ptr->layerspecs[layer].n;
  double nt = In_Ptr->layerspecs[layer-1].n;
  
  /* Get r. */
  if( - uz <= In_Ptr->layerspecs[layer].cos_crit0) 
    r=1.0;		      /* total internal reflection. */
  else r = RFresnel(ni, nt, -uz, &uz1);
  
#if PARTIALREFLECTION
  if(layer == 1 && r<1.0) {	/* partially transmitted. */
    Photon_Ptr->uz = -uz1;	/* transmitted photon. */
    RecordR(r, In_Ptr, Photon_Ptr, Out_Ptr);
    Photon_Ptr->uz = -uz;	/* reflected photon. */
  }		
  else if(RandomNum() > r) {/* transmitted to layer-1. */
    Photon_Ptr->layer--;
    Photon_Ptr->ux *= ni/nt;
    Photon_Ptr->uy *= ni/nt;
    Photon_Ptr->uz = -uz1;
  }
  else			      		/* reflected. */
    Photon_Ptr->uz = -uz;
#else
  if(RandomNum() > r) {		/* transmitted to layer-1. */
    if(layer==1)  {
      Photon_Ptr->uz = -uz1;
      RecordR(0.0, In_Ptr, Photon_Ptr, Out_Ptr);
      Photon_Ptr->dead = 1;
    }
    else {
      Photon_Ptr->layer--;
      Photon_Ptr->ux *= ni/nt;
      Photon_Ptr->uy *= ni/nt;
      Photon_Ptr->uz = -uz1;
    }
  }
  else 						/* reflected. */
    Photon_Ptr->uz = -uz;
#endif
}

/***********************************************************
 *	Decide whether the photon will be transmitted  or be 
 *	reflected on the bottom boundary (uz>0) of the current 
 *	layer.
 *
 *	If the photon is transmitted, move the photon to 
 *	"layer+1". If "layer" is the last layer, record the 
 *	transmitted weight as transmittance. See comments for 
 *	CrossUpOrNot.
 *
 *	Update the photon parmameters.
 ****/
void CrossDnOrNot(InputStruct  *	In_Ptr, 
				  PhotonStruct *	Photon_Ptr,
				  OutStruct *		Out_Ptr)
{
  double uz = Photon_Ptr->uz; /* z directional cosine. */
  double uz1;	/* cosines of transmission alpha. */
  double r=0.0;	/* reflectance */
  short  layer = Photon_Ptr->layer;
  double ni = In_Ptr->layerspecs[layer].n;
  double nt = In_Ptr->layerspecs[layer+1].n;
  
  /* Get r. */
  if( uz <= In_Ptr->layerspecs[layer].cos_crit1) 
    r=1.0;		/* total internal reflection. */
  else r = RFresnel(ni, nt, uz, &uz1);
  
#if PARTIALREFLECTION	
  if(layer == In_Ptr->num_layers && r<1.0) {
    Photon_Ptr->uz = uz1;
    RecordT(r, In_Ptr, Photon_Ptr, Out_Ptr);
    Photon_Ptr->uz = -uz;
  }
  else if(RandomNum() > r) {/* transmitted to layer+1. */
    Photon_Ptr->layer++;
    Photon_Ptr->ux *= ni/nt;
    Photon_Ptr->uy *= ni/nt;
    Photon_Ptr->uz = uz1;
  }
  else 						/* reflected. */
    Photon_Ptr->uz = -uz;
#else
  if(RandomNum() > r) {		/* transmitted to layer+1. */
    if(layer == In_Ptr->num_layers) {
      Photon_Ptr->uz = uz1;
      RecordT(0.0, In_Ptr, Photon_Ptr, Out_Ptr);
      Photon_Ptr->dead = 1;
    }
    else {
      Photon_Ptr->layer++;
      Photon_Ptr->ux *= ni/nt;
      Photon_Ptr->uy *= ni/nt;
      Photon_Ptr->uz = uz1;
    }
  }
  else 						/* reflected. */
    Photon_Ptr->uz = -uz;
#endif
}

/***********************************************************
 ****/
void CrossOrNot(InputStruct  *	In_Ptr, 
				PhotonStruct *	Photon_Ptr,
				OutStruct    *	Out_Ptr)
{
  if(Photon_Ptr->uz < 0.0)
    CrossUpOrNot(In_Ptr, Photon_Ptr, Out_Ptr);
  else
    CrossDnOrNot(In_Ptr, Photon_Ptr, Out_Ptr);
}

/***********************************************************
 *	Move the photon packet in glass layer.
 *	Horizontal photons are killed because they will
 *	never interact with tissue again.
 ****/
void HopInGlass(InputStruct  * In_Ptr,
				PhotonStruct * Photon_Ptr,
				OutStruct    * Out_Ptr)
{
  double dl;     /* step size. 1/cm */
  
  if(Photon_Ptr->uz == 0.0) { 
	/* horizontal photon in glass is killed. */
    Photon_Ptr->dead = 1;
  }
  else {
    StepSizeInGlass(Photon_Ptr, In_Ptr);
    Hop(Photon_Ptr);
    CrossOrNot(In_Ptr, Photon_Ptr, Out_Ptr);
  }
}

/***********************************************************
 *	Set a step size, move the photon, drop some weight, 
 *	choose a new photon direction for propagation.  
 *
 *	When a step size is long enough for the photon to 
 *	hit an interface, this step is divided into two steps. 
 *	First, move the photon to the boundary free of 
 *	absorption or scattering, then decide whether the 
 *	photon is reflected or transmitted.
 *	Then move the photon in the current or transmission 
 *	medium with the unfinished stepsize to interaction 
 *	site.  If the unfinished stepsize is still too long, 
 *	repeat the above process.  
 ****/
void HopDropSpinInTissue(InputStruct  *  In_Ptr,
						 PhotonStruct *  Photon_Ptr,
						 OutStruct    *  Out_Ptr)
{
  StepSizeInTissue(Photon_Ptr, In_Ptr);
  
  if(HitBoundary(Photon_Ptr, In_Ptr)) {
    Hop(Photon_Ptr);	/* move to boundary plane. */
    CrossOrNot(In_Ptr, Photon_Ptr, Out_Ptr);
  }
  else {
    Hop(Photon_Ptr);

#ifdef NO_ABS
    DropNoAbs(In_Ptr, Photon_Ptr, Out_Ptr);
#else
    Drop(In_Ptr, Photon_Ptr, Out_Ptr);
#endif 

    Spin(In_Ptr->layerspecs[Photon_Ptr->layer].g, 
		Photon_Ptr);
  }
}

/***********************************************************
 ****/
void HopDropSpin(InputStruct  *  In_Ptr,
				 PhotonStruct *  Photon_Ptr,
				 OutStruct    *  Out_Ptr)
{
  short layer = Photon_Ptr->layer;

  if((In_Ptr->layerspecs[layer].mua == 0.0) 
  && (In_Ptr->layerspecs[layer].mus == 0.0)) 
	/* glass layer. */
    HopInGlass(In_Ptr, Photon_Ptr, Out_Ptr);
  else
    HopDropSpinInTissue(In_Ptr, Photon_Ptr, Out_Ptr);
  
  if( Photon_Ptr->w < In_Ptr->Wth && !Photon_Ptr->dead) 
    Roulette(Photon_Ptr);
}
