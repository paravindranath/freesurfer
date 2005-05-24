#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "macros.h"
#include "mrisurf.h"
#include "mrisutils.h"
#include "error.h"
#include "diag.h"
#include "mri.h"
#include "mri2.h"
#include "version.h"

static int  parse_commandline(int argc, char **argv);
static void check_options(void);
static void print_usage(void) ;
static void usage_exit(void);
static void print_help(void) ;
static void print_version(void) ;
static void argnerr(char *option, int n);
static void dump_options(FILE *fp);
static int  singledash(char *flag);

typedef struct {
  int id;
  char name[1000];
  int nhits;
  float vol;
  float min, max, range, mean, std;
} STATSUMENTRY;

int MRIsegFrameAvg(MRI *seg, int segid, MRI *mri, double *favg);
int *MRIsegIdList(MRI *seg, int *nlist, int frame);
int MRIsegCount(MRI *seg, int id, int frame);
int MRIsegStats(MRI *seg, int segid, MRI *mri,	int frame, 
		float *min, float *max, float *range, 
		float *mean, float *std);
int compare_ints(const void *v1,const void *v2);
int nunqiue_int_list(int *idlist, int nlist);
int *unqiue_int_list(int *idlist, int nlist, int *nunique);

int main(int argc, char *argv[]) ;

static char vcid[] = "$Id: mri_segstats.c,v 1.2 2005/05/24 20:39:02 greve Exp $";
char *Progname = NULL, *SUBJECTS_DIR = NULL;
char *SegVolFile = NULL;
char *InVolFile = NULL;
char *MaskVolFile = NULL;
char *subject = NULL;
char *StatTableFile = NULL;
char *FrameAvgFile = NULL;
char *FrameAvgVolFile = NULL;
int DoFrameAvg = 0;
int frame = 0;
int synth = 0;
int debug = 0;
long seed = 0;
MRI *seg, *invol, *famri, *maskvol;
int nsegid, *segidlist;
int NonEmptyOnly = 0;
int UserSegIdList[1000];
int nUserSegIdList = 0;
int DoExclSegId = 0, ExclSegId = 0;

float maskthresh = 0.5;
int   maskinvert = 0, maskframe = 0;
char *masksign;
int   nmaskhits;

char *ctabfile = NULL;
COLOR_TABLE *ctab = NULL;
STATSUMENTRY *StatSumTable = NULL;
STATSUMENTRY *StatSumTable2 = NULL;

/*--------------------------------------------------*/
int main(int argc, char **argv)
{
  int nargs, n, nhits, f, nsegidrep, ind, nthsegid;
  int c,r,s;
  float voxelvolume;
  float min, max, range, mean, std;
  FILE *fp;
  double  **favg;

  /* rkt: check for and handle version tag */
  nargs = handle_version_option (argc, argv, vcid, "$Name:  $");
  if (nargs && argc - nargs == 1) exit (0);
  argc -= nargs;

  Progname = argv[0] ;
  argc --;
  argv++;
  ErrorInit(NULL, NULL, NULL) ;
  DiagInit(NULL, NULL, NULL) ;

  if(argc == 0) usage_exit();

  parse_commandline(argc, argv);
  check_options();
  dump_options(stdout);

  if(subject != NULL){
    SUBJECTS_DIR = getenv("SUBJECTS_DIR");
    if(SUBJECTS_DIR==NULL){
      fprintf(stderr,"ERROR: SUBJECTS_DIR not defined in environment\n");
      exit(1);
    }
  }

  /* Make sure we can open the output summary table file*/
  fp = fopen(StatTableFile,"w");
  if(fp == NULL){
    printf("ERROR: could not open %s for writing\n",StatTableFile);
    exit(1);
  }
  fclose(fp);

  /* Make sure we can open the output frame average file*/
  if(FrameAvgFile != NULL){
    fp = fopen(FrameAvgFile,"w");
    if(fp == NULL){
      printf("ERROR: could not open %s for writing\n",FrameAvgFile);
      exit(1);
    }
    fclose(fp);
  }

  /* Load the segmentation */
  printf("Loading %s\n",SegVolFile);
  seg = MRIread(SegVolFile);
  if(seg == NULL){
    printf("ERROR: loading %s\n",SegVolFile);
    exit(1);
  }

  /* Load the input volume */
  if(InVolFile != NULL){
    printf("Loading %s\n",InVolFile);
    invol = MRIread(InVolFile);
    if(invol == NULL){
      printf("ERROR: loading %s\n",InVolFile);
      exit(1);
    }
    if(frame >= invol->nframes){
      printf("ERROR: input frame = %d, input volume only has %d frames\n",
	     frame,invol->nframes);
      exit(1);
    }
    /* Should check that invol the same dim as seg, etc*/
  }

  /* Load the mask volume */
  if(MaskVolFile != NULL){
    printf("Loading %s\n",MaskVolFile);
    maskvol = MRIread(MaskVolFile);
    if(maskvol == NULL){
      printf("ERROR: loading %s\n",MaskVolFile);
      exit(1);
    }
    if(maskframe >= maskvol->nframes){
      printf("ERROR: mask frame = %d, mask volume only has %d frames\n",
	     maskframe,maskvol->nframes);
      exit(1);
    }
    /* Should check that maskvol the same dim as seg, etc*/
    mri_binarize(maskvol, maskthresh, masksign, maskinvert,
		 maskvol, &nmaskhits);
    if(nmaskhits == 0){
      printf("ERROR: no voxels in mask meet thresholding criteria\n");
      exit(1);
    }
    printf("There were %d voxels in the mask\n",nmaskhits);

    /* perform the masking */
    for(c=0; c < seg->width; c++){
      for(r=0; r < seg->height; r++){
	for(s=0; s < seg->depth; s++){
	  // Set voxels out of the mask to 0
	  if(! (int)MRIgetVoxVal(maskvol,c,r,s,maskframe))
	    MRIsetVoxVal(seg,c,r,s,0,0);
	}
      }
    }
  }

  voxelvolume = seg->xsize * seg->ysize * seg->zsize;
  printf("Voxel Volume is %g mm^3\n",voxelvolume);

  /* There are three ways that the list of segmentations
     can be specified:
     1. User does not specify, then get all from the seg itself
     2. User specfies with --id (can be multiple)
     3. User supplies a color table
     If the user specficies a color table and --id, then the
     segs from --id are used ang the color table is only
     used to determine the name of the segmentation.
   */

  if(ctabfile == NULL && nUserSegIdList == 0){
    /* Must get list of segmentation ids from segmentation itself*/
    printf("Generating list of segmentation ids\n");
    segidlist = MRIsegIdList(seg, &nsegid,0);
    StatSumTable = (STATSUMENTRY *) calloc(sizeof(STATSUMENTRY),nsegid);
    for(n=0; n < nsegid; n++){
      StatSumTable[n].id = segidlist[n];
      strcpy(StatSumTable[n].name, "\0");
    }
  }
  else{ /* Get from user or color table */
    if(ctabfile != NULL){
      /* Load the color table file */
      ctab = CTABread(ctabfile);
      if(ctab == NULL){
	printf("ERROR: reading %s\n",ctabfile);
	exit(1);
      }
      if(nUserSegIdList == 0){
	/* User has not spec anything, so use all the ids in the color table */
	nsegid = ctab->nbins;
	StatSumTable = (STATSUMENTRY *) calloc(sizeof(STATSUMENTRY),nsegid);
	for(n=0; n < nsegid; n++){
	  StatSumTable[n].id = ctab->bins[n].index;
	  strcpy(StatSumTable[n].name, ctab->bins[n].name);
	}
      } else {
	/* User has specified --id, use those and get names from ctab */
	nsegid = nUserSegIdList;
	StatSumTable = (STATSUMENTRY *) calloc(sizeof(STATSUMENTRY),nsegid);
	for(n=0; n < nsegid; n++){
	  StatSumTable[n].id = UserSegIdList[n];
	  ind = CTABindexToItemNo(ctab,StatSumTable[n].id);
	  if(ind == -1){
	    printf("ERROR: cannot find seg id %d in %s\n",
		   StatSumTable[n].id,ctabfile);
	    exit(1);
	  }
	  strcpy(StatSumTable[n].name, ctab->bins[ind].name);
	}
      }
    } else { /* User specified ids, but no color table */
      nsegid = nUserSegIdList;
      StatSumTable = (STATSUMENTRY *) calloc(sizeof(STATSUMENTRY),nsegid);
      for(n=0; n < nsegid; n++)
	StatSumTable[n].id = UserSegIdList[n];
    }
  }
  printf("Found %3d segmentations\n",nsegid);

  printf("Computing statistics for each segmentation\n");
  for(n=0; n < nsegid; n++){
    printf("%3d ",n);
    if(n%20 == 19) printf("\n");
    fflush(stdout);
    nhits = MRIsegCount(seg, StatSumTable[n].id, 0);
    StatSumTable[n].nhits = nhits;
    if(InVolFile != NULL){
      if(nhits > 0){
	MRIsegStats(seg, StatSumTable[n].id, invol, frame,
		    &min, &max, &range, &mean, &std);
      } 
      else {min=0;max=0;range=0;mean=0;std=0;}
      StatSumTable[n].min   = min;
      StatSumTable[n].max   = max;
      StatSumTable[n].range = range;
      StatSumTable[n].mean  = mean;
      StatSumTable[n].std   = std;
    }
  }
  printf("\n");


  /* Remove empty segmentations, if desired */
  if(NonEmptyOnly || DoExclSegId){
    // Count the number of nonempty segmentations
    nsegidrep = 0;
    for(n=0; n < nsegid; n++){
      if(NonEmptyOnly && StatSumTable[n].nhits==0) continue;
      if(DoExclSegId && StatSumTable[n].id==ExclSegId) continue;
      nsegidrep ++;
    }
    StatSumTable2 = (STATSUMENTRY *) calloc(sizeof(STATSUMENTRY),nsegidrep);
    nthsegid = 0;
    for(n=0; n < nsegid; n++){
      if(NonEmptyOnly && StatSumTable[n].nhits==0) continue;
      if(DoExclSegId && StatSumTable[n].id==ExclSegId) continue;
      StatSumTable2[nthsegid].id    = StatSumTable[n].id;
      StatSumTable2[nthsegid].nhits = StatSumTable[n].nhits;
      StatSumTable2[nthsegid].min   = StatSumTable[n].min;
      StatSumTable2[nthsegid].max   = StatSumTable[n].max;
      StatSumTable2[nthsegid].range = StatSumTable[n].range;
      StatSumTable2[nthsegid].mean  = StatSumTable[n].mean;
      StatSumTable2[nthsegid].std   = StatSumTable[n].std;
      strcpy(StatSumTable2[nthsegid].name,StatSumTable[n].name);
      nthsegid++;
    }
    free(StatSumTable);
    StatSumTable = StatSumTable2; 
    nsegid = nsegidrep;
  }
  printf("Reporting on %3d segmentations\n",nsegid);

  /* Dump the table to the screen */
  if(debug){
    for(n=0; n < nsegid; n++){
      printf("%3d  %8d %10.1f  ", StatSumTable[n].id,StatSumTable[n].nhits,
	     voxelvolume*StatSumTable[n].nhits);
      if(ctabfile != NULL) printf("%-30s ",StatSumTable[n].name);
      if(InVolFile != NULL)
	printf("%10.4f %10.4f %10.4f %10.4f %10.4f ", 
	       StatSumTable[n].min, StatSumTable[n].max, 
	       StatSumTable[n].range, StatSumTable[n].mean, 
	       StatSumTable[n].std);
      printf("\n");
    }
  }

  /* Print the table to the output file */
  if(StatTableFile != NULL){
    fp = fopen(StatTableFile,"w");
    fprintf(fp,"# Title Segmentation Statistics \n");
    fprintf(fp,"# SegVolFile %s \n",SegVolFile);
    if(ctabfile)  fprintf(fp,"# ColorTable %s \n",ctabfile);
    if(InVolFile) {
      fprintf(fp,"# InVolFile  %s \n",InVolFile);
      fprintf(fp,"#   InVolFrame %d \n",frame);
    }
    if(MaskVolFile) {
      fprintf(fp,"# MaskVolFile  %s \n",MaskVolFile);
      fprintf(fp,"#   MaskThresh %f \n",maskthresh);
      fprintf(fp,"#   MaskSign   %s \n",masksign);
      fprintf(fp,"#   MaskFrame  %d \n",maskframe);
      fprintf(fp,"#   MaskInvert %d \n",maskframe);
    }
    if(DoExclSegId)  fprintf(fp,"# ExcludeSegId %d \n",ExclSegId);
    if(NonEmptyOnly) fprintf(fp,"# Only reporting non-empty segmentations\n");
    fprintf(fp,"# VoxelVolume_mm3 %g \n",voxelvolume);
    fprintf(fp,"# Col 1 Index \n");
    fprintf(fp,"# Col 2 SegId \n");
    fprintf(fp,"# Col 3 NVoxels \n");
    fprintf(fp,"# Col 4 Volume_mm3 \n");
    n = 5;
    if(ctabfile) {fprintf(fp,"# Col %d SegName \n",n); n++;}
    if(InVolFile) {
      fprintf(fp,"# Col %d Min \n",n);    n++;
      fprintf(fp,"# Col %d Max \n",n);    n++;
      fprintf(fp,"# Col %d Range \n",n);  n++;
      fprintf(fp,"# Col %d Mean \n",n);   n++;
      fprintf(fp,"# Col %d StdDev \n",n); n++;
    }
    fprintf(fp,"# NCols %d \n",n-1); 
    fprintf(fp,"# NRows %d \n",nsegid);
    for(n=0; n < nsegid; n++){
      fprintf(fp,"%3d %3d  %8d %10.1f  ", n+1, StatSumTable[n].id,
	      StatSumTable[n].nhits,
	      voxelvolume*StatSumTable[n].nhits);
      if(ctabfile != NULL) fprintf(fp,"%-30s ",StatSumTable[n].name);
      if(InVolFile != NULL)
	fprintf(fp,"%10.4f %10.4f %10.4f %10.4f %10.4f ", 
	       StatSumTable[n].min, StatSumTable[n].max, 
	       StatSumTable[n].range, StatSumTable[n].mean, 
	       StatSumTable[n].std);
      fprintf(fp,"\n");
    }
    fclose(fp);
  }

  // Average input across space to create a waveform 
  // for each segmentation
  if(DoFrameAvg){
    printf("Computing frame average\n");
    favg = (double **) calloc(sizeof(double *),nsegid);
    for(n=0; n < nsegid; n++)
      favg[n] = (double *) calloc(sizeof(double),invol->nframes);
    for(n=0; n < nsegid; n++){
      printf("%3d",n);
      if(n%20 == 19) printf("\n");
      fflush(stdout);
      MRIsegFrameAvg(seg, StatSumTable[n].id, invol, favg[n]);
    }
    printf("\n");

    // Save as a simple text file
    if(FrameAvgFile){
      printf("Writing to %s\n",FrameAvgFile);
      fp = fopen(FrameAvgFile,"w");
      for(f=0; f < invol->nframes; f++){
	fprintf(fp,"%3d %7.3f ",f,f*invol->tr/1000);
	for(n=0; n < nsegid; n++) fprintf(fp,"%g ",favg[n][f]);
	fprintf(fp,"\n");
      }
      fclose(fp);
    }

    // Save as an MRI "volume"
    if(FrameAvgVolFile){
      printf("Writing to %s\n",FrameAvgVolFile);
      famri = MRIallocSequence(nsegid,1,1,MRI_FLOAT,invol->nframes);
      for(f=0; f < invol->nframes; f++){
	for(n=0; n < nsegid; n++)
	  MRIsetVoxVal(famri,n,0,0,f,(float)favg[n][f]);
      }
      MRIwrite(famri,FrameAvgVolFile);
    }
  }// Done with Frame Average

  return(0);
}
/*-----------------------------------------------------------------*/
/*-----------------------------------------------------------------*/
/*-----------------------------------------------------------------*/

/* --------------------------------------------- */
static int parse_commandline(int argc, char **argv)
{
  int  nargc , nargsused;
  char **pargv, *option ;

  if(argc < 1) usage_exit();

  nargc   = argc;
  pargv = argv;
  while(nargc > 0){

    option = pargv[0];
    if(debug) printf("%d %s\n",nargc,option);
    nargc -= 1;
    pargv += 1;

    nargsused = 0;

    if (!strcasecmp(option, "--help"))  print_help() ;
    else if (!strcasecmp(option, "--version")) print_version() ;
    else if (!strcasecmp(option, "--debug"))   debug = 1;
    else if (!strcasecmp(option, "--nonempty")) NonEmptyOnly = 1;
    else if ( !strcmp(option, "--seg") ) {
      if(nargc < 1) argnerr(option,1);
      SegVolFile = pargv[0];
      nargsused = 1;
    }
    else if ( !strcmp(option, "--in") ) {
      if(nargc < 1) argnerr(option,1);
      InVolFile = pargv[0];
      nargsused = 1;
    }
    else if ( !strcmp(option, "--id") ) {
      if(nargc < 1) argnerr(option,1);
      sscanf(pargv[0],"%d",&UserSegIdList[nUserSegIdList]);
      nUserSegIdList++;
      nargsused = 1;
    }
    else if ( !strcmp(option, "--excludeid") ) {
      if(nargc < 1) argnerr(option,1);
      sscanf(pargv[0],"%d",&ExclSegId);
      DoExclSegId = 1;
      nargsused = 1;
    }

     else if ( !strcmp(option, "--mask") ) {
      if(nargc < 1) argnerr(option,1);
      MaskVolFile = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--masksign")){
      if(nargc < 1) argnerr(option,1);
      masksign = pargv[0];
      nargsused = 1;
      if(strncasecmp(masksign,"abs",3) &&
	 strncasecmp(masksign,"pos",3) &&
	 strncasecmp(masksign,"neg",3)){
	fprintf(stderr,"ERROR: mask sign = %s, must be abs, pos, or neg\n",
		masksign);
	exit(1);
      }
    }
    else if (!strcmp(option, "--maskthresh")){
      if(nargc < 1) argnerr(option,1);
      sscanf(pargv[0],"%f",&maskthresh);
      nargsused = 1;
    }
    else if (!strcmp(option, "--maskframe")){
      if(nargc < 1) argnerr(option,1);
      sscanf(pargv[0],"%d",&maskframe);
      nargsused = 1;
    }
    else if (!strcasecmp(option, "--maskinvert"))  maskinvert = 1;

    else if ( !strcmp(option, "--sum") ) {
      if(nargc < 1) argnerr(option,1);
      StatTableFile = pargv[0];
      nargsused = 1;
    }
    else if ( !strcmp(option, "--avgwf") ) {
      if(nargc < 1) argnerr(option,1);
      FrameAvgFile = pargv[0];
      DoFrameAvg = 1;
      nargsused = 1;
    }
    else if ( !strcmp(option, "--avgwfvol") ) {
      if(nargc < 1) argnerr(option,1);
      FrameAvgVolFile = pargv[0];
      DoFrameAvg = 1;
      nargsused = 1;
    }
    else if ( !strcmp(option, "--ctab") ) {
      if(nargc < 1) argnerr(option,1);
      ctabfile = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--frame")){
      if(nargc < 1) argnerr(option,1);
      sscanf(pargv[0],"%d",&frame);
      nargsused = 1;
    }
    else if (!strcmp(option, "--subject")){
      if(nargc < 1) argnerr(option,1);
      subject = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--synth")){
      if(nargc < 1) argnerr(option,1);
      sscanf(pargv[0],"%ld",&seed);
      synth = 1;
      nargsused = 1;
    }
    else{
      fprintf(stderr,"ERROR: Option %s unknown\n",option);
      if(singledash(option))
	fprintf(stderr,"       Did you really mean -%s ?\n",option);
      exit(-1);
    }
    nargc -= nargsused;
    pargv += nargsused;
  }
  return(0);
}
/* ------------------------------------------------------ */
static void usage_exit(void)
{
  print_usage() ;
  exit(1) ;
}
/* --------------------------------------------- */
static void print_usage(void)
{
  printf("USAGE: %s \n",Progname) ;
  printf("\n");
  printf("   --seg segvol : segmentation volume path \n");
  printf("   --sum file   : stats summary table file \n");
  printf("\n");
  printf(" Other Options\n");
  printf("   --in invol : report more stats on the input volume\n");
  printf("   --frame frame : report stats on nth frame of input volume\n");
  printf("\n");
  printf("   --ctab ctabfile : color table file with seg id names\n");
  printf("   --id segid <--id segid> : manually specify seg ids\n");
  printf("   --excludeid segid : exclude seg id from report\n");
  printf("   --nonempty : only report non-empty segmentations\n");
  printf("\n");
  printf("Masking options\n");
  printf("   --mask maskvol : must be same size as seg \n");
  printf("   --maskthresh thresh : binarize mask with this threshold <0.5>\n");
  printf("   --masksign sign : <abs>,pos,neg\n");
  printf("   --maskframe frame : 0-based frame number <0>\n");
  printf("   --maskinvert : invert mask \n");
  printf("\n");
  printf("Average waveform options\n");
  printf("   --avgwf textfile  : save into an ascii file\n");
  printf("   --avgwfvol mrivol : save as a binary mri 'volume'\n");
  printf("\n");
  printf("   --help      print out information on how to use this program\n");
  printf("   --version   print out version and exit\n");
  printf("\n");
  printf("%s\n", vcid) ;
  printf("\n");
}
/* --------------------------------------------- */
static void print_help(void)
{
  print_usage() ;

  printf(
"\n"
"Help Outline:\n"
"  - SUMMARY\n"
"  - COMMAND-LINE ARGUMENTS\n"
"  - SPECIFYING SEGMENTATION IDS\n"
"  - SUMMARY FILE FORMAT\n"
"  - EXAMPLES\n"
"  - SEE ALSO\n"
"\n"
"SUMMARY\n"
"\n"
"This program will comute statistics on segmented volumes. In its\n"
"simplist invocation, it will report on the number of voxels and\n"
"volume in each segmentation. However, it can also compute statistics\n"
"on the segmentation based on the values from another volume. This\n"
"includes computing waveforms averaged inside each segmentation.\n"
"\n"
"COMMAND-LINE ARGUMENTS\n"
"\n"
"--seg segvol\n"
"\n"
"Input segmentation volume. A segmentation is a volume whose voxel\n"
"values indicate a segmentation or class. This can be as complicaated\n"
"as a FreeSurfer automatic cortical or subcortial segmentation or as\n"
"simple as a binary mask. The format of segvol can be anything that\n"
"mri_convert accepts as input (eg, analyze, nifti, mgh, bhdr, bshort, \n"
"bfloat).\n"
"\n"
"--sum summaryfile\n"
"\n"
"ASCII file in which summary statistics are saved. See SUMMARY FILE\n"
"below for more information.\n"
"\n"
"--in invol\n"
"\n"
"Input volume from which to compute more statistics, including min,\n"
"max, range, average, and standard deviation as measured spatially\n"
"across each segmentation. The input volume must be the same size\n"
"and dimension as the segmentation volume.\n"
"\n"
"--frame frame\n"
"\n"
"Report statistics of the input volume at the 0-based frame number.\n"
"frame is 0 be default.\n"
"\n"
"--ctab ctabfile\n"
"\n"
"FreeSurfer color table file. This is a file used by FreeSurfer to \n"
"specify how each segmentation index is mapped to a segmentation\n"
"name and color. See $FREESURFER_HOME/tkmeditColorsCMA for example.\n"
"The ctab can be used to specify the segmentations to report on or\n"
"simply to supply human-readable names to segmentations chosen with\n"
"--id. See SPECIFYING SEGMENTATION IDS below.\n"
"\n"
"--id segid1 <--id segid2>\n"
"\n"
"Specify numeric segmentation ids. Multiple ids can be specified with\n"
"multiple --id invocations. SPECIFYING SEGMENTATION IDS.\n"
"\n"
"--excludeid segid\n"
"\n"
"Exclude the given segmentation id from report. This can be convenient\n"
"for removing id=0. Only one segid can be targeted for exclusion.\n"
"\n"
"--nonempty\n"
"\n"
"Only report on segmentations that have actual representations in the\n"
"segmentation volume.\n"
"\n"
"--mask maskvol\n"
"\n"
"Exlude voxels that are not in the mask. Voxels to be excluded are\n"
"assigned a segid of 0. The mask volume may be binary or continuous.\n"
"The masking criteria is set by the mask threshold, sign, frame, and\n"
"invert parameters (see below). The mask volume must be the same\n"
"size and dimension as the segmentation volume. If no voxels meet \n"
"the masking criteria, then mri_segstats exits with an error.\n"
"\n"
"--maskthresh thresh\n"
"\n"
"Exlude voxels that are below thresh (for pos sign), above -thresh (for\n"
"neg sign), or between -thresh and +thresh (for abs sign). Default\n"
"is 0.5.\n"
"\n"
"--masksign sign\n"
"\n"
"Specify sign for masking threshold. Choices are abs, pos, and neg. \n"
"Default is abs.\n"
"\n"
"--maskframe frame\n"
"\n"
"Derive the mask volume from the 0-based frameth frame.\n"
"\n"
"--maskinvert\n"
"\n"
"After applying all the masking criteria, invert the mask.\n"
"\n"
"--avgwf textfile\n"
"\n"
"For each segmentation, compute an average waveform across all the\n"
"voxels in the segmentation (excluding voxels masked out). The results\n"
"are saved in an ascii text file with number of rows equal to the\n"
"number of frames and number of columns equal to the number of\n"
"segmentations reported plus 2. The first two columns are: (1) 0-based\n"
"frame number and (2) 0-based frame number times TR.\n"
"\n"
"--avgwfvol mrivol\n"
"\n"
"Same as --avgwf except that the resulting waveforms are stored in a\n"
"binary mri volume format (eg, analyze, nifti, mgh, etc) with number of\n"
"columns equal to the number segmentations, number of rows = slices =\n"
"1, and the number of frames equal that of the input volume. This may\n"
"be more convenient than saving as an ascii text file.\n"
"\n"
"--help\n"
"\n"
"Don't get me started ...\n"
"\n"
"SPECIFYING SEGMENTATION IDS\n"
"\n"
"There are three ways that the list of segmentations to report on\n"
"can be specified:\n"
"  1. User specfies with --id.\n"
"  2. User supplies a color table but does not specify --id. All\n"
"     the segmentations in the color table are then reported on.\n"
"     If the user specficies a color table and --id, then the\n"
"     segids from --id are used and the color table is only\n"
"     used to determine the name of the segmentation for reporint\n"
"     purposes.\n"
"  3. If the user does not specify either --id or a color table, then \n"
"     all the ids from the segmentation volume are used.\n"
"This list can be further reduced by specifying masks, --nonempty,\n"
"and --excludeid.\n"
"\n"
"SUMMARY FILE FORMAT\n"
"\n"
"The summary file is an ascii file in which the segmentation statistics\n"
"are reported. This file will have some 'header' information. Each\n"
"header line begins with a '#'. There will be a row for each\n"
"segmentation reported. The number and meaning of the columns depends\n"
"somewhat how the program was run. The indentity of each column is\n"
"given in the header. The first col is the row number. The second col\n"
"is the segmentation id. The third col is the number of voxels in the\n"
"segmentation. The fourth col is the volume of the segmentation in\n"
"mm. If a color table was specified, then the next column will be the\n"
"segmentation name. If an input volume was specified, then the next\n"
"five columns will be intensity min, max, range, average, and standard\n"
"deviation measured across the voxels in the segmentation.\n"
"\n"
"EXAMPLES\n"
"\n"
"1. mri_segstats --seg $SUBJECTS_DIR/bert/mri/aseg \n"
"    --ctab $FREESURFER_HOME/tkmeditColorsCMA \n"
"    --nonempty --excludeid 0 --sum bert.aseg.sum \n"
"\n"
"This will compute the segmentation statistics from the automatic\n"
"FreeSurfer subcortical segmentation for non-empty segmentations and\n"
"excluding segmentation 0 (UNKNOWN). The results are stored in\n"
"bert.aseg.sum.\n"
"\n"
"2. mri_segstats --seg $SUBJECTS_DIR/bert/mri/aseg \n"
"    --ctab $FREESURFER_HOME/tkmeditColorsCMA \n"
"    --nonempty --excludeid 0 --sum bert.aseg.sum \n"
"    --in $SUBJECTS_DIR/bert/mri/orig\n"
"\n"
"Same as above but intensity statistics from the orig volume\n"
"will also be reported for each segmentation.\n"
"\n"
"3. mri_segstats --seg aseg-in-func.img \n"
"    --ctab $FREESURFER_HOME/tkmeditColorsCMA \n"
"    --nonempty --excludeid 0 --in func.img \n"
"    --mask spmT.img --maskthresh 2.3 \n"
"    --sum bert.aseg-in-func.sum \n"
"    --avgwf bert.avgwf.dat --avgwfvol bert.avgwf.img\n"
"\n"
"This will compute the segmentation statistics from the automatic\n"
"FreeSurfer subcortical segmentation resampled into the functional\n"
"space (see below and mri_label2vol --help). It will report intensity\n"
"statistics from the 4D analyze volume func.img (same dimension as\n"
"aseg-in-func.img). The segmentation is masked by thresholding the\n"
"spmT.img map at 2.3. The average functional waveform of each\n"
"segmentation is reported in the ascii file bert.avgwf.dat and in the\n"
"4D analyze 'volume' bert.avgwf.img. This is not a real volume but just\n"
"another way to save the data that may be more convenient than ascii.\n"
"\n"
"4. mri_label2vol --seg $SUBJECTS_DIR/bert/mri/aseg \n"
"     --temp func.img --reg register.dat \n"
"     --fillthresh 0.5 --o aseg-in-func.img\n"
"\n"
"This uses mri_label2vol to resample the automatic subcortical\n"
"segmentation to the functional space. For more information\n"
"see mri_label2vol --help.\n"
"\n"
"5. mri_label2vol --seg $SUBJECTS_DIR/bert/label/lh.aparc.annot \n"
"     --temp func.img --reg register.dat --fillthresh 0.5 \n"
"     --hemi lh --subject bert --proj frac 0 .1 1 \n"
"     --o lh.aparc-in-func.img\n"
"\n"
"This uses mri_label2vol to resample the automatic cortical\n"
"segmentation to the functional space. For more information\n"
"see mri_label2vol --help.\n"
"\n"
"SEE ALSO:\n"
"  mri_label2vol, tkregister2, mri_vol2roi.\n"
"\n"
"\n"
);

  exit(1) ;
}
/* --------------------------------------------- */
static void print_version(void)
{
  printf("%s\n", vcid) ;
  exit(1) ;
}
/* --------------------------------------------- */
static void argnerr(char *option, int n)
{
  if(n==1)
    fprintf(stderr,"ERROR: %s flag needs %d argument\n",option,n);
  else
    fprintf(stderr,"ERROR: %s flag needs %d arguments\n",option,n);
  exit(-1);
}
/* --------------------------------------------- */
static void check_options(void)
{
  if(SegVolFile == NULL){
    printf("ERROR: must specify a segmentation volume\n");
    exit(1);
  }
  if(StatTableFile == NULL){
    printf("ERROR: must specify an output table file\n");
    exit(1);
  }
  if(DoFrameAvg && InVolFile == NULL){
    printf("ERROR: cannot do frame average without input volume\n");
    exit(1);
  }

  return;
}

/* --------------------------------------------- */
static void dump_options(FILE *fp)
{
  return;
}
/*---------------------------------------------------------------*/
static int singledash(char *flag)
{
  int len;
  len = strlen(flag);
  if(len < 2) return(0);

  if(flag[0] == '-' && flag[1] != '-') return(1);
  return(0);
}







/* ----------------------------------------------------------
   MRIsegCount() - returns the number of times the given 
   segmentation id appears in the volume.
   --------------------------------------------------------- */
int MRIsegCount(MRI *seg, int id, int frame)
{
  int nhits, v, c,r,s;
  nhits = 0;
  for(c=0; c < seg->width; c++){
    for(r=0; r < seg->height; r++){
      for(s=0; s < seg->depth; s++){
	v = (int) MRIgetVoxVal(seg,c,r,s,frame);
	if(v == id) nhits ++;
      }
    }
  }
  return(nhits);
}
/* ----------------------------------------------------------
   MRIsegIdList() - returns a list of the unique segmentation ids in
   the volume. The number in the list is *nlist. The volume need not
   be an int or char, but it is probably what it will be.
   --------------------------------------------------------- */
int *MRIsegIdList(MRI *seg, int *nlist, int frame)
{
  int nvoxels,r,c,s,nth;
  int *tmplist = NULL;
  int *segidlist = NULL;

  nvoxels = seg->width * seg->height * seg->depth;
  tmplist = (int *) calloc(sizeof(int),nvoxels);

  // First, load all voxels into a list
  nth = 0;
  for(c=0; c < seg->width; c++){
    for(r=0; r < seg->height; r++){
      for(s=0; s < seg->depth; s++){
	tmplist[nth] = (int) MRIgetVoxVal(seg,c,r,s,frame);
	nth++;
      }
    }
  }

  segidlist = unqiue_int_list(tmplist, nvoxels, nlist);
  free(tmplist);
  //for(nth=0; nth < *nlist; nth++)
  //printf("%3d %3d\n",nth,segidlist[nth]);
  return(segidlist);
}

/* --------------------------------------------- */
int compare_ints(const void *v1,const void *v2)
{
  int i1, i2;

  i1 = *((int*)v1);
  i2 = *((int*)v2);
  
  if(i1 < i2) return(-1);
  if(i1 > i2) return(+1);
  return(0);
}
/* --------------------------------------------------- 
   nunqiue_int_list() - counts the number of unique items
   in a list of integers. The list will be sorted.
   --------------------------------------------------- */
int nunqiue_int_list(int *idlist, int nlist)
{
  int idprev, nunique, n;

  qsort(idlist,nlist,sizeof(int),compare_ints);
  nunique = 1;
  idprev = idlist[0];
  for(n=1; n<nlist; n++){
    if(idprev != idlist[n]){
      nunique++;
      idprev = idlist[n];
    }
  }
  return(nunique);
}
/* --------------------------------------------------- 
   unqiue_int_list() - the returns the unique items
   in a list of integers. The list will be sorted.
   --------------------------------------------------- */
int *unqiue_int_list(int *idlist, int nlist, int *nunique)
{
  int n, *ulist, nthu;

  /* count number of unique elements in the list,
     this also sorts the list */
  *nunique = nunqiue_int_list(idlist, nlist);

  /* alloc the unqiue list */
  ulist = (int *) calloc(sizeof(int),*nunique);

  nthu = 0;
  ulist[nthu] = idlist[0];
  for(n=1; n<nlist; n++){
    if(ulist[nthu] != idlist[n]){
      nthu ++;
      ulist[nthu] = idlist[n];
    }
  }
  return(ulist);
}

/*---------------------------------------------------------
  MRIsegStats() - computes statistics within a given
  segmentation. Returns the number of voxels in the
  segmentation.
  ---------------------------------------------------------*/
int MRIsegStats(MRI *seg, int segid, MRI *mri,int frame, 
		float *min, float *max, float *range, 
		float *mean, float *std)
{
  int id,nvoxels,r,c,s;
  double val, sum, sum2;

  *min = 0;
  *max = 0;
  sum  = 0;
  sum2 = 0;
  nvoxels = 0;
  for(c=0; c < seg->width; c++){
    for(r=0; r < seg->height; r++){
      for(s=0; s < seg->depth; s++){
	id = (int) MRIgetVoxVal(seg,c,r,s,0);
	if(id != segid) continue;
	val =  MRIgetVoxVal(mri,c,r,s,frame);
	nvoxels++;
	if( nvoxels == 1 ){
	  *min = val;
	  *max = val;
	}
	if(*min > val) *min = val;
	if(*max < val) *max = val;
	sum  += val;
	sum2 += (val*val);
      }
    }
  }

  *range = *max - *min;

  if(nvoxels != 0) *mean = sum/nvoxels;
  else             *mean = 0.0;

  if(nvoxels > 1)
    *std = sqrt(((nvoxels)*(*mean)*(*mean) - 2*(*mean)*sum + sum2)/
		(nvoxels-1));
  else *std = 0.0;

  return(nvoxels);
}
/*---------------------------------------------------------
  MRIsegFrameAvg() - computes the average time course withing the
  given segmentation. Returns the number of voxels in the
  segmentation. favg must be preallocated to number of
  frames. favg = (double *) calloc(sizeof(double),mri->nframes);
  ---------------------------------------------------------*/
int MRIsegFrameAvg(MRI *seg, int segid, MRI *mri, double *favg)
{
  int id,nvoxels,r,c,s,f;
  double val;

  /* zero it out */
  for(f=0;f<mri->nframes;f++) favg[f] = 0;

  nvoxels = 0;
  for(c=0; c < seg->width; c++){
    for(r=0; r < seg->height; r++){
      for(s=0; s < seg->depth; s++){
	id = (int) MRIgetVoxVal(seg,c,r,s,0);
	if(id != segid) continue;
	for(f=0;f<mri->nframes;f++){
	  val =  MRIgetVoxVal(mri,c,r,s,f);
	  favg[f] += val;
	}
	nvoxels++;
      }
    }
  }

  if(nvoxels != 0)
    for(f=0;f<mri->nframes;f++) favg[f] /= nvoxels;

  return(nvoxels);
}

