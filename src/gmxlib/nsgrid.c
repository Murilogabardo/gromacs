/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * Gnomes, ROck Monsters And Chili Sauce
 */
static char *SRCID_nsgrid_c = "$Id$";

#include "assert.h"
#include "sysstuff.h"
#include "typedefs.h"
#include "macros.h"
#include "smalloc.h"
#include "nsgrid.h"
#include "fatal.h"
#include "vec.h"
#include "network.h"

#define NO_CELL -1

/***********************************
 *         Grid Routines
 ***********************************/

static void _range_check(char *s,int i,int nr,char *file,int line)
{
  if ((i<0) || (i>=nr))
    fatal_error(0,"%s = %d should be in 0 .. %d [FILE %s, LINE %d]",
		s,i,nr-1,file,line);
}
#define range_check(i,nr) _range_check(#i,i,nr,__FILE__,__LINE__)

void init_grid(FILE *log,t_grid *grid,int delta,matrix box,real rlong,int ncg)
{
  int     m;
  ivec    cx;

  for(m=0; (m<DIM); m++) 
    cx[m]=(delta*box[m][m])/rlong; 

  grid->nr      = ncg;
  grid->nrx     = cx[XX];
  grid->nry     = cx[YY];
  grid->nrz     = cx[ZZ];
  grid->ncells  = cx[XX]*cx[YY]*cx[ZZ];
  grid->maxcells= 2*grid->ncells;
  grid->delta	= delta;
  grid->gmax    = 0;
  snew(grid->cell_index,grid->nr+1);
  snew(grid->a,grid->nr+1);
  snew(grid->index,grid->maxcells);
  snew(grid->nra,grid->maxcells);
  
  fprintf(log,"Grid: %d x %d x %d cells\n",
	  grid->nrx,grid->nry,grid->nrz);
    
  if (debug) 
    fprintf(log,"Succesfully allocated memory for grid pointers.");
}

int xyz2ci_(int nry,int nrz,int x,int y,int z)
/* Return the cell index */
{
  return (nry*nrz*x+nrz*y+z);
}

void ci2xyz(t_grid *grid, int i, int *x, int *y, int *z)
/* Return x,y and z from the cell index */
{
  int ci;

  range_check(i,grid->nr);

  ci = grid->cell_index[i];
  if (ci == NO_CELL)
    fatal_error(0,"Not a valid cell entry at %d\n",i);
  *x  = ci / (grid->nry*grid->nrz);
  ci -= (*x)*grid->nry*grid->nrz;
  *y  = ci / grid->nrz;
  ci -= (*y)*grid->nrz;
  *z  = ci;
}

void grid_first(FILE *log,t_grid *grid,matrix box,real rlong)
{
  int    *nra=grid->nra;
  int    i,k,ncells;
  ivec   cx;

  for(k=0; (k<DIM); k++)
    cx[k]=(grid->delta*box[k][k])/rlong;

  grid->nrx    = cx[XX];
  grid->nry    = cx[YY];
  grid->nrz    = cx[ZZ];
  ncells       = cx[XX]*cx[YY]*cx[ZZ];

  if (grid->ncells != ncells) {
    fprintf(log,"Grid: %d x %d x %d cells\n",
	    grid->nrx,grid->nry,grid->nrz);
    if (ncells > grid->maxcells) { 
      srenew(grid->nra,ncells);
      srenew(grid->index,ncells);
      for(i=grid->maxcells; (i<ncells); i++) {
	grid->nra[i] = 0;
	grid->index[i] = 0;
      }
      fprintf(log,"WARNING: your box is exploding! (ncells = %d)\n",ncells);
      grid->maxcells = ncells;
    }
    grid->ncells = ncells;
  }
  
  for(i=0; (i<ncells); i++)
    nra[i]=0;
}

static void calc_bor(FILE *log,bool bDD,
		     int cg0,int cg1,int ncg,int CG0[2],int CG1[2])
{
  if (bDD) {
    CG0[0] = cg0;
    CG0[1] = 0;
    CG1[0] = cg1;
    CG1[1] = 0;
  }
  else {
    if (cg1 > ncg) {
      CG0[0]=cg0;
      CG1[0]=ncg;
      CG0[1]=0;
      CG1[1]=cg1-ncg;
    }
    else {
      CG0[0]=cg0;
      CG1[0]=cg1;
      CG0[1]=0;
      CG1[1]=0;
    }
  }
  if (debug) {
    int m;
    
    fprintf(log,"calc_bor: cg0=%d, cg1=%d, ncg=%d\n",cg0,cg1,ncg);
    for(m=0; (m<2); m++)
      fprintf(log,"CG0[%d]=%d, CG1[%d]=%d\n",m,CG0[m],m,CG1[m]);
  }

}

void calc_elemnr(FILE *log,bool bDD,int cg_index[],
		 t_grid *grid,int cg0,int cg1,int ncg)
{
  int    CG0[2],CG1[2];
  int    *cell_index=grid->cell_index;
  int    *nra=grid->nra;
  int    i,m,ncells;
  int    ci;

  ncells=grid->ncells;
  calc_bor(log,bDD,cg0,cg1,ncg,CG0,CG1);
  for(m=0; (m<2); m++)
    for(i=CG0[m]; (i<CG1[m]); i++) {
      ci = cell_index[i];
      range_check(ci,ncells);
      nra[ci]++;
    }
}

void calc_ptrs(t_grid *grid)
{
  int *index = grid->index;
  int *nra   = grid->nra;
  int ix,iy,iz,ci,nr;
  int nnra,ncells;
  int gmax     = 0;

  ncells=grid->ncells;
  ci=nr=0;
  for(ix=0; (ix < grid->nrx); ix++)
    for(iy=0; (iy < grid->nry); iy++) 
      for(iz=0; (iz < grid->nrz); iz++,ci++) {
	range_check(ci,ncells);
	index[ci] = nr;
	nnra      = nra[ci];
	nr       += nnra;
	gmax      = max(gmax,nnra);
	nra[ci]   = 0;
      }
  grid->gmax=gmax;
}

void grid_last(FILE *log,bool bDD,int cg_index[],
	       t_grid *grid,int cg0,int cg1,int ncg)
{
  int    CG0[2],CG1[2];
  int    i,m;
  int    ci,ind,ncells;
  int    *cell_index = grid->cell_index;
  int    *nra        = grid->nra;
  int    *index      = grid->index;
  int    *a          = grid->a;

  ncells=grid->ncells;
  calc_bor(log,bDD,cg0,cg1,ncg,CG0,CG1);
  for(m=0; (m<2); m++)
    for(i=CG0[m]; (i<CG1[m]); i++) {
      ci     = cell_index[i];
      range_check(ci,ncells);
      ind    = index[ci]+nra[ci]++;
      range_check(ind,grid->nr);
      a[ind] = cg_index[i];
    }
}

void fill_grid(FILE *log,bool bDD,int cg_index[],
	       t_grid *grid,matrix box,
	       int ncg,int cg0,int cg1,rvec cg_cm[])
{
  int    *cell_index=grid->cell_index;
  int    nrx,nry,nrz;
  real   dx,dy,dz;
  int  	 i,index,ix,iy,iz;
  int    ci;
  
  /* Initiate cell borders */
  nrx = grid->nrx;
  nry = grid->nry;
  nrz = grid->nrz;
  dx  = divide(nrx,box[XX][XX]);
  dy  = divide(nry,box[YY][YY]);
  dz  = divide(nrz,box[ZZ][ZZ]);

  /* Assign cell indices to charge groups */
  for (i=0; (i<cg0); i++) {
    cell_index[i]=NO_CELL;
  }
  
  if (debug)
    fprintf(debug,"Filling grid from %d to %d (total %d)\n",cg0,cg1,ncg);

  /* We assume here that the charge group center of mass is allways
   * 0 <= cgcm < box
   * If not this will generate errors (SEGV). If you suspect this, turn on
   * DEBUG_PBC
   */
  for (i=cg0; (i<cg0+cg1); i++) {
    index = cg_index[i];
    ix    = dx*cg_cm[index][XX];
    iy    = dy*cg_cm[index][YY];
    iz    = dz*cg_cm[index][ZZ];
    if (ix >= nrx) ix = nrx-1;
    if (iy >= nry) iy = nry-1;
    if (iz >= nrz) iz = nrz-1;
#ifdef DEBUG_PBC
    assert((ix >= 0) && (ix < nrx));
    assert((iy >= 0) && (iy < nry));
    assert((iz >= 0) && (iz < nrz));
#endif
    ci    = xyz2ci(nry,nrz,ix,iy,iz);
    cell_index[i] = ci;
  }
  for (; (i<ncg); i++) {
    cell_index[i]=NO_CELL;
  }
}

void check_grid(FILE *log,t_grid *grid)
{
  int ix,iy,iz,ci,cci,nra;

  ci=0;
  cci=0;
  for(ix=0; (ix<grid->nrx); ix++)
    for(iy=0; (iy<grid->nry); iy++)
      for(iz=0; (iz<grid->nrz); iz++,ci++) {
	if (ci > 0) {
	  nra=grid->index[ci]-grid->index[cci];
	  if (nra != grid->nra[cci]) 
	    fatal_error(0,"nra=%d, grid->nra=%d, cci=%d",
			nra,grid->nra[cci],cci);
	}
	cci=xyz2ci(grid->nry,grid->nrz,ix,iy,iz);
	range_check(cci,grid->ncells);
	
	if (cci != ci) 
	  fatal_error(0,"ci = %d, cci = %d",ci,cci);
      }
}

void print_grid(FILE *log,t_grid *grid,bool bDD,int cg_index[])
{
  int i,nra,index;
  int ix,iy,iz,ci;

  fprintf(log,"nr:    %d\n",grid->nr);
  fprintf(log,"nrx:   %d\n",grid->nrx);
  fprintf(log,"nry:   %d\n",grid->nry);
  fprintf(log,"nrz:   %d\n",grid->nrz);
  fprintf(log,"delta: %d\n",grid->delta);
  fprintf(log,"gmax:  %d\n",grid->gmax);
  fprintf(log,"    i  cell_index\n");
  for(i=0; (i<grid->nr); i++)
    fprintf(log,"%5d  %5d\n",i,grid->cell_index[i]);
  fprintf(log,"cells\n");
  fprintf(log," ix iy iz   nr  index  cgs...\n");
  ci=0;
  for(ix=0; (ix<grid->nrx); ix++)
    for(iy=0; (iy<grid->nry); iy++)
      for(iz=0; (iz<grid->nrz); iz++,ci++) {
	index=grid->index[ci];
	nra=grid->nra[ci];
	fprintf(log,"%3d%3d%3d%5d%5d",ix,iy,iz,nra,index);
	for(i=0; (i<nra); i++)
	  fprintf(log,"%5d",grid->a[index+i]);
	fprintf(log,"\n");
      }
  fflush(log);
}

void mv_grid(t_commrec *cr,bool bDD,int cg_index[],
	     t_grid *grid,int cgload[])
{
  int i,start,nr;
  int cur=cr->pid;
  int *ci;
#define next ((cur+1) % cr->nprocs)

  ci=grid->cell_index;
  for(i=0; (i<cr->nprocs-1); i++) {
    start=(cur == 0) ? 0 : cgload[cur-1];
    nr=cgload[cur]-start;
    gmx_tx(cr->left,&(ci[start]),nr*sizeof(*ci));
    
    start=(next == 0) ? 0 : cgload[next-1];
    nr=cgload[next]-start;
    gmx_rx(cr->right,&(ci[start]),nr*sizeof(*ci));
    
    gmx_tx_wait(cr->left);
    gmx_rx_wait(cr->right);
    
    cur=next;
  }
}

