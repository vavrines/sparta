/* ----------------------------------------------------------------------
   SPARTA - Stochastic PArallel Rarefied-gas Time-accurate Analyzer
   http://sparta.sandia.gov
   Steve Plimpton, sjplimp@sandia.gov, Michael Gallis, magalli@sandia.gov
   Sandia National Laboratories

   Copyright (2014) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level SPARTA directory.
------------------------------------------------------------------------- */

#include "mpi.h"
#include "math.h"
#include "string.h"
#include "stdlib.h"
#include "particle.h"
#include "grid.h"
#include "update.h"
#include "comm.h"
#include "mixture.h"
#include "random_park.h"
#include "memory.h"
#include "error.h"

using namespace SPARTA_NS;

enum{PKEEP,PINSERT,PDONE,PDISCARD,PENTRY,PEXIT};   // same as several files

#define DELTA 10000
#define DELTASPECIES 16
#define MAXLINE 1024
#define CHUNK_MIXTURE 8

// customize by adding an abbreviation string
// also add a check for the keyword in 2 places in add_species()

#define AIR "N O NO"

/* ---------------------------------------------------------------------- */

Particle::Particle(SPARTA *sparta) : Pointers(sparta)
{
  exist = 0;
  nglobal = 0;
  nlocal = maxlocal = 0;
  particles = NULL;

  nspecies = maxspecies  = 0;
  species = NULL;

  //maxgrid = 0;
  //cellcount = NULL;
  //first = NULL;
  maxsort = 0;
  next = NULL;

  // create two default mixtures

  nmixture = maxmixture = 0;
  mixture = NULL;

  char **newarg = new char*[1];
  newarg[0] = (char *) "all";
  add_mixture(1,newarg);
  newarg[0] = (char *) "species";
  add_mixture(1,newarg);
  delete [] newarg;
}

/* ---------------------------------------------------------------------- */

Particle::~Particle()
{
  memory->sfree(species);
  for (int i = 0; i < nmixture; i++) delete mixture[i];
  memory->sfree(mixture);

  memory->sfree(particles);
  //memory->destroy(cellcount);
  //memory->destroy(first);
  memory->destroy(next);
}

/* ---------------------------------------------------------------------- */

void Particle::init()
{
  for (int i = 0; i < nmixture; i++) mixture[i]->init();

  // reallocate cellcount and first lists as needed
  // NOTE: when grid becomes dynamic, will need to do this in sort()

  //if (maxgrid < grid->nlocal) {
  //  maxgrid = grid->nlocal;
    //    memory->destroy(cellcount);
    //memory->destroy(first);
    //memory->create(first,maxgrid,"particle:first");
    //memory->create(cellcount,maxgrid,"particle:cellcount");
  // }
}

/* ----------------------------------------------------------------------
   compress particle list to remove particles with indices in mlist
   mlist indices MUST be in ascending order
   overwrite deleted particle with particle from end of nlocal list
   inner while loop avoids overwrite with deleted particle at end of mlist
   called from Comm::migrate_particles() when particles migrate every step
------------------------------------------------------------------------- */

void Particle::compress(int nmigrate, int *mlist)
{
  int j,k;
  int nbytes = sizeof(OnePart);

  for (int i = 0; i < nmigrate; i++) {
    j = mlist[i];
    k = nlocal - 1;
    while (k == mlist[nmigrate-1] && k > j) {
      nmigrate--;
      nlocal--;
      k--;
    }
    memcpy(&particles[j],&particles[k],nbytes);
    nlocal--;
  }
}

/* ----------------------------------------------------------------------
   compress particle list to remove particles with icell < 0
   all particles MUST be in owned cells
   overwrite deleted particle with particle from end of nlocal list
   called from Comm::migrate_cells() when cells+particles migrate on rebalance
------------------------------------------------------------------------- */

void Particle::compress()
{
  int nbytes = sizeof(OnePart);

  int i = 0;
  while (i < nlocal) {
    if (particles[i].icell < 0) {
      memcpy(&particles[i],&particles[nlocal-1],nbytes);
      nlocal--;
    } else i++;
  }
}

/* ----------------------------------------------------------------------
   sort particles into grid cells
------------------------------------------------------------------------- */

void Particle::sort()
{
  // reallocate next list as needed

  if (maxsort < maxlocal) {
    maxsort = maxlocal;
    memory->destroy(next);
    memory->create(next,maxsort,"particle:next");
  }

  // initialize linked list of particles in cells I own

  Grid::ChildCell *cells = grid->cells;
  Grid::ChildInfo *cinfo = grid->cinfo;
  int nglocal = grid->nlocal;

  for (int icell = 0; icell < nglocal; icell++) {
    cinfo[icell].first = -1;
    cinfo[icell].count = 0;
    //cellcount[i] = 0;
    //first[i] = -1;
  }

  // reverse loop stores linked list in forward order
  // icell = global cell the particle is in

  int icell;
  for (int i = nlocal-1; i >= 0; i--) {
    icell = particles[i].icell;
    next[i] = cinfo[icell].first;
    cinfo[icell].first = i;
    cinfo[icell].count++;

    // NOTE: this method seems much slower for some reason
    // uses separate, smaller vectors for first & cellcount
    //icell = cells[particles[i].icell].local;
    //next[i] = first[icell];
    //first[icell] = i;
    //cellcount[icell]++;
  }
}

/* ----------------------------------------------------------------------
   insure particle list can hold nextra new particles
------------------------------------------------------------------------- */

void Particle::grow(int nextra)
{
  bigint target = (bigint) nlocal + nextra;
  if (target <= maxlocal) return;
  
  int oldmax = maxlocal;
  bigint newmax = maxlocal;
  while (newmax < target) newmax += DELTA;
  
  if (newmax > MAXSMALLINT) 
    error->one(FLERR,"Per-processor grid count is too big");

  maxlocal = newmax;
  particles = (OnePart *)
    memory->srealloc(particles,maxlocal*sizeof(OnePart),
		     "particle:particles");
  memset(&particles[oldmax],0,(maxlocal-oldmax)*sizeof(OnePart));
}

/* ----------------------------------------------------------------------
   add a particle to particle list
------------------------------------------------------------------------- */

void Particle::add_particle(int id, int ispecies, int icell,
			    double *x, double *v, double erot, int ivib)
{
  if (nlocal == maxlocal) grow(1);

  OnePart *p = &particles[nlocal];

  p->id = id;
  p->ispecies = ispecies;
  p->icell = icell;
  p->x[0] = x[0];
  p->x[1] = x[1];
  p->x[2] = x[2];
  p->v[0] = v[0];
  p->v[1] = v[1];
  p->v[2] = v[2];
  p->erot = erot;
  p->ivib = ivib;
  p->flag = PKEEP;
  //p->dtremain = 0.0;    not needed due to memset ??

  nlocal++;
}

/* ----------------------------------------------------------------------
   add one or more species to species list
------------------------------------------------------------------------- */

void Particle::add_species(int narg, char **arg)
{
  if (narg < 2) error->all(FLERR,"Illegal species command");

  if (comm->me == 0) {
    fp = fopen(arg[0],"r");
    if (fp == NULL) {
      char str[128];
      sprintf(str,"Cannot open species file %s",arg[0]);
      error->one(FLERR,str);
    }
  }

  // filespecies = list of species defined in file

  nfilespecies = maxfilespecies = 0;
  filespecies = NULL;

  if (comm->me == 0) read_species_file();
  MPI_Bcast(&nfilespecies,1,MPI_INT,0,world);
  if (nfilespecies >= maxfilespecies) {
    memory->destroy(filespecies);
    maxfilespecies = nfilespecies;
    filespecies = (Species *) 
      memory->smalloc(maxfilespecies*sizeof(Species),
		      "particle:filespecies");
  }
  MPI_Bcast(filespecies,nfilespecies*sizeof(Species),MPI_BYTE,0,world);

  // newspecies = # of new user-requested species
  // names = list of new species IDs
  // customize abbreviations by adding new keyword in 2 places

  char line[MAXLINE];

  int newspecies = 0;
  for (int iarg = 1; iarg < narg; iarg++) {
    if (strcmp(arg[iarg],"air") == 0) {
      strcpy(line,AIR);
      newspecies += wordcount(line,NULL);
    } else newspecies++;
  }

  char **names = new char*[newspecies];
  newspecies = 0;

  for (int iarg = 1; iarg < narg; iarg++) {
    if (strcmp(arg[iarg],"air") == 0) {
      strcpy(line,AIR);
      newspecies += wordcount(line,&names[newspecies]);
    } else names[newspecies++] = arg[iarg];
  }

  // extend species list if necessary

  if (nspecies + newspecies > maxspecies) {
    while (nspecies+newspecies > maxspecies) maxspecies += DELTASPECIES;
    species = (Species *) 
      memory->srealloc(species,maxspecies*sizeof(Species),"particle:species");
  }

  // extract info on user-requested species from file species list
  // add new species to default mixtures "all" and "species"

  int imix_all = find_mixture((char *) "all");
  int imix_species = find_mixture((char *) "species");

  int j;

  for (int i = 0; i < newspecies; i++) {
    for (j = 0; j < nspecies; j++)
      if (strcmp(names[i],species[j].id) == 0) break;
    if (j < nspecies) error->all(FLERR,"Species ID is already defined");
    for (j = 0; j < nfilespecies; j++)
      if (strcmp(names[i],filespecies[j].id) == 0) break;
    if (j == nfilespecies)
      error->all(FLERR,"Species ID does not appear in species file");
    memcpy(&species[nspecies],&filespecies[j],sizeof(Species));
    nspecies++;

    mixture[imix_all]->add_species_default(species[nspecies-1].id);
    mixture[imix_species]->add_species_default(species[nspecies-1].id);
  }

  memory->sfree(filespecies);
  delete [] names;
}

/* ----------------------------------------------------------------------
   create or augment a mixture of species
------------------------------------------------------------------------- */

void Particle::add_mixture(int narg, char **arg)
{
  if (narg < 1) error->all(FLERR,"Illegal mixture command");

  // imix = index if mixture ID already exists
  // else instantiate a new mixture

  int imix = find_mixture(arg[0]);

  if (imix < 0) {
    if (nmixture == maxmixture) {
      maxmixture += CHUNK_MIXTURE;
      mixture = (Mixture **) memory->srealloc(mixture,
                                              maxmixture*sizeof(Mixture *),
                                              "particle:mixture");
    }
    imix = nmixture;
    mixture[nmixture++] = new Mixture(sparta,arg[0]);
  }

  mixture[imix]->command(narg,arg);
}

/* ----------------------------------------------------------------------
   return index of ID in list of species IDs
   return -1 if not found
------------------------------------------------------------------------- */

int Particle::find_species(char *id)
{
  for (int i = 0; i < nspecies; i++)
    if (strcmp(id,species[i].id) == 0) return i;
  return -1;
}

/* ----------------------------------------------------------------------
   return index of ID in list of mixture IDs
   return -1 if not found
------------------------------------------------------------------------- */

int Particle::find_mixture(char *id)
{
  for (int i = 0; i < nmixture; i++)
    if (strcmp(id,mixture[i]->id) == 0) return i;
  return -1;
}

/* ----------------------------------------------------------------------
   generate random rotational energy for a particle
   only a function of species index and species properties
------------------------------------------------------------------------- */

double Particle::erot(int isp, RanPark *random)
{
 double eng,a,erm,b;

 if (species[isp].rotdof < 2) return 0.0;

 if (species[isp].rotdof == 2)
   eng = -log(random->uniform()) * update->boltz * update->temp_thermal;
 else {
   a = 0.5*particle->species[isp].rotdof-1.;
   while (1) {
     // energy cut-off at 10 kT
     erm = 10.0*random->uniform();
     b = pow(erm/a,a) * exp(a-erm);
     if (b > random->uniform()) break;
   }
   // NOTE: is temp_thermal always set?
   eng = erm * update->boltz * update->temp_thermal;
 }

 return eng;
}

/* ----------------------------------------------------------------------
   generate random vibrational energy for a particle
   only a function of species index and species properties
------------------------------------------------------------------------- */

int Particle::evib(int isp, RanPark *random)
{
  if (species[isp].rotdof < 2) return 0;

  // NOTE: is temp_thermal always set?

  int ivib = -log(random->uniform()) * update->temp_thermal /
    particle->species[isp].vibtemp;
  return ivib;
}

/* ----------------------------------------------------------------------
   read list of species defined in species file
   store info in filespecies and nfilespecies
   only invoked by proc 0
------------------------------------------------------------------------- */

void Particle::read_species_file()
{
  nfilespecies = maxfilespecies = 0;
  filespecies = NULL;

  // read file line by line
  // skip blank lines or comment lines starting with '#'
  // all other lines must have NWORDS 

  int NWORDS = 10;
  char **words = new char*[NWORDS];
  char line[MAXLINE],copy[MAXLINE];

  while (fgets(line,MAXLINE,fp)) {
    int pre = strspn(line," \t\n");
    if (pre == strlen(line) || line[pre] == '#') continue;

    strcpy(copy,line);
    int nwords = wordcount(copy,NULL);
    if (nwords != NWORDS)
      error->one(FLERR,"Incorrect line format in species file");

    if (nfilespecies == maxfilespecies) {
      maxfilespecies += DELTASPECIES;
      filespecies = (Species *) 
	memory->srealloc(filespecies,maxfilespecies*sizeof(Species),
			 "particle:filespecies");
    }

    nwords = wordcount(line,words);
    Species *fsp = &filespecies[nfilespecies];

    if (strlen(words[0]) + 1 > 16)
      error->one(FLERR,"Invalid species ID in species file");
    strcpy(fsp->id,words[0]);

    fsp->molwt = atof(words[1]);
    fsp->mass = atof(words[2]);
    fsp->rotdof = atoi(words[3]);
    fsp->rotrel = atoi(words[4]);
    fsp->vibdof = atoi(words[5]);
    fsp->vibrel = atoi(words[6]);
    fsp->vibtemp = atof(words[7]);
    fsp->specwt = atof(words[8]);
    fsp->charge = atof(words[9]);

    nfilespecies++;
  }

  delete [] words;

  fclose(fp);
}

/* ----------------------------------------------------------------------
   count whitespace-delimited words in line
   line will be modified, since strtok() inserts NULLs
   if words is non-NULL, store ptr to each word
------------------------------------------------------------------------- */

int Particle::wordcount(char *line, char **words)
{
  int nwords = 0;
  char *word = strtok(line," \t");

  while (word) {
    if (words) words[nwords] = word;
    nwords++;
    word = strtok(NULL," \t");
  }

  return nwords;
}

/* ---------------------------------------------------------------------- */

bigint Particle::memory_usage()
{
  bigint bytes = (bigint) maxlocal * sizeof(OnePart);
  bytes += (bigint) maxlocal * sizeof(int);
  return bytes;
}
