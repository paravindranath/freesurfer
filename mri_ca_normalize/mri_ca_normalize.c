

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mri.h"
#include "matrix.h"
#include "proto.h"
#include "macros.h"
#include "error.h"
#include "timer.h"
#include "diag.h"
#include "mrimorph.h"
#include "utils.h"
#include "gca.h"
#include "cma.h"
#include "mrinorm.h"
#include "version.h"

char         *Progname ;

static double TRs[MAX_GCA_INPUTS] ;
static double fas[MAX_GCA_INPUTS] ;
static double TEs[MAX_GCA_INPUTS] ;

static int file_only = 0 ;
static char *normalized_transformed_sample_fname = NULL ;
static char *mask_fname = NULL ;
static char *sample_fname = NULL ;
static char *ctl_point_fname = NULL ;
static int novar = 0 ;

static float min_prior = 0.6 ;
static FILE *diag_fp = NULL ;


static void usage_exit(int code) ;
static int get_option(int argc, char *argv[]) ;

static char *renormalization_fname = NULL ;
static double TR = 0.0, TE = 0.0, alpha = 0.0 ;
static char *tissue_parms_fname = NULL ;
static char *example_T1 = NULL ;
static char *example_segmentation = NULL ;
static double min_region_prior(GCA *gca, int xp, int yp, int zp, int wsize, int label) ;
static GCA_SAMPLE *find_control_points(GCA *gca, GCA_SAMPLE *gcas, int total_nsamples, 
                                       int *pnorm_samples, int nregions, int label,
                                       MRI *mri_in, TRANSFORM *transform, double min_prior,
                                       double ctrl_point_pct) ;

static GCA_SAMPLE *gcas_concatenate(GCA_SAMPLE *gcas1, GCA_SAMPLE *gcas2, int n1, int n2);
static int  gcas_bounding_box(GCA_SAMPLE *gcas, int nsamples, int *pxmin, int *pymin, int *pzmin, 
                              int *pxmax, int *pymax, int *pzmax, int label) ;
static int  uniform_region(GCA *gca, MRI *mri, TRANSFORM *transform, int x, int y, int z, int wsize, GCA_SAMPLE *gcas, float nsigma) ;
static int  discard_unlikely_control_points(GCA *gca, GCA_SAMPLE *gcas_struct, int struct_samples, 
																						MRI *mri_in, TRANSFORM *transform, char *name) ;

/* 
   command line consists of three inputs:

   argv[1]  - input volume
   argv[2]  - atlas (gca)
   argv[3]  - transform (lta/xfm/m3d)
   argv[4]  - output volume
*/

#define DEFAULT_CTL_POINT_PCT   .25
static double ctl_point_pct = DEFAULT_CTL_POINT_PCT ;

static int normalization_structures[] =
{
  Left_Cerebral_White_Matter,
  Right_Cerebral_White_Matter,
  Left_Cerebellum_White_Matter,
  Right_Cerebellum_White_Matter,
  Brain_Stem
} ;

#define NSTRUCTURES (sizeof(normalization_structures) / sizeof(normalization_structures[0]))

static int nregions = 3 ;  /* divide each struct into 3x3x3 regions */

int
main(int argc, char *argv[])
{
  char         *gca_fname, *in_fname, *out_fname, **av, *xform_fname ;
  MRI          *mri_in, *mri_norm = NULL, *mri_tmp ;
  GCA          *gca ;
  int          ac, nargs, nsamples, msec, minutes, seconds, i, struct_samples, norm_samples, n, input, ninputs ;
  struct timeb start ;
  GCA_SAMPLE   *gcas, *gcas_norm = NULL, *gcas_struct ;
  TRANSFORM    *transform = NULL ;

  /* rkt: check for and handle version tag */
  nargs = handle_version_option (argc, argv, "$Id: mri_ca_normalize.c,v 1.13 2003/06/13 15:24:26 fischl Exp $");
  if (nargs && argc - nargs == 1)
    exit (0);
  argc -= nargs;

  setRandomSeed(-1L) ;
  Progname = argv[0] ;

  DiagInit(NULL, NULL, NULL) ;
  ErrorInit(NULL, NULL, NULL) ;

  ac = argc ;
  av = argv ;
  for ( ; argc > 1 && ISOPTION(*argv[1]) ; argc--, argv++)
  {
    nargs = get_option(argc, argv) ;
    argc -= nargs ;
    argv += nargs ;
  }

  if (argc < 4)
    ErrorExit(ERROR_BADPARM, 
              "usage: %s <in brain> <atlas> <transform file> <output file name>\n",
              Progname) ;

	ninputs = (argc - 3) / 2 ;
	printf("reading %d input volume\n", ninputs) ;	
  in_fname = argv[1] ;
  gca_fname = argv[1+ninputs] ;
  xform_fname = argv[2+ninputs] ;
  out_fname = argv[3+ninputs] ;

  TimerStart(&start) ;
  printf("reading atlas from '%s'...\n", gca_fname) ;
  fflush(stdout) ;
  gca = GCAread(gca_fname) ;
  if (gca == NULL)
    ErrorExit(ERROR_NOFILE, "%s: could not open GCA %s.\n",
              Progname, gca_fname) ;

  printf("reading transform from '%s'...\n", xform_fname) ;
  fflush(stdout) ;
  transform = TransformRead(xform_fname) ;
  if (!transform)
    ErrorExit(ERROR_BADPARM, "%s: could not open xform file %s", Progname,xform_fname) ;

  if (novar)
    GCAunifyVariance(gca) ;

  if (renormalization_fname)
  {
    FILE   *fp ;
    int    *labels, nlines, i ;
    float  *intensities, f1, f2 ;
    char   *cp, line[STRLEN] ;

    fp = fopen(renormalization_fname, "r") ;
    if (!fp)
      ErrorExit(ERROR_NOFILE, "%s: could not read %s",
                Progname, renormalization_fname) ;

    cp = fgetl(line, 199, fp) ;
    nlines = 0 ;
    while (cp)
    {
      nlines++ ;
      cp = fgetl(line, 199, fp) ;
    }
    rewind(fp) ;
    printf("reading %d labels from %s...\n", nlines,renormalization_fname) ;
    labels = (int *)calloc(nlines, sizeof(int)) ;
    intensities = (float *)calloc(nlines, sizeof(float)) ;
    cp = fgetl(line, 199, fp) ;
    for (i = 0 ; i < nlines ; i++)
    {
      sscanf(cp, "%e  %e", &f1, &f2) ;
      labels[i] = (int)f1 ; intensities[i] = f2 ;
      if (labels[i] == Left_Cerebral_White_Matter)
        DiagBreak() ;
      cp = fgetl(line, 199, fp) ;
    }
    GCArenormalizeIntensities(gca, labels, intensities, nlines) ;
    free(labels) ; free(intensities) ;
  }



	for (input = 0 ; input < ninputs ; input++)
	{
		in_fname = argv[1+input] ;
		printf("reading input volume from %s...\n", in_fname) ;
		mri_tmp = MRIread(in_fname) ;
		if (!mri_tmp)
			ErrorExit(ERROR_NOFILE, "%s: could not read input MR volume from %s",
								Progname, in_fname) ;
		
		if (alpha > 0)
			mri_tmp->flip_angle = alpha ;
		if (TR > 0)
			mri_tmp->tr = TR ;
		if (TE > 0)
			mri_tmp->te = TE ;

		TRs[input] = mri_tmp->tr ;
		fas[input] = mri_tmp->flip_angle ;
		TEs[input] = mri_tmp->te ;

		if (input == 0)
		{
			mri_in = 
				MRIallocSequence(mri_tmp->width, mri_tmp->height, mri_tmp->depth,
												 mri_tmp->type, ninputs) ;
			if (!mri_in)
				ErrorExit(ERROR_NOMEMORY, 
									"%s: could not allocate input volume %dx%dx%dx%d",
									mri_tmp->width, mri_tmp->height, mri_tmp->depth,ninputs) ;
			MRIcopyHeader(mri_tmp, mri_in) ;
		}

	  if (mask_fname)
  	{
			int i ;
	    MRI *mri_mask ;

  	  mri_mask = MRIread(mask_fname) ;
    	if (!mri_mask)
	      ErrorExit(ERROR_NOFILE, "%s: could not open mask volume %s.\n",
  	              Progname, mask_fname) ;

		
			for (i = 1 ; i < WM_MIN_VAL ; i++)
				MRIreplaceValues(mri_mask, mri_mask, i, 0) ;
    	MRImask(mri_tmp, mri_mask, mri_tmp, 0, 0) ;
	    MRIfree(&mri_mask) ;
  	}
		MRIcopyFrame(mri_tmp, mri_in, 0, input) ;
		MRIfree(&mri_tmp) ;
	}

	if (gca->type == GCA_FLASH)
	{
		GCA *gca_tmp ;

		printf("mapping %d-dimensional flash atlas into %d-dimensional input space\n",
			gca->ninputs, ninputs) ;
		if (novar)
			GCAunifyVariance(gca) ;

		gca_tmp = GCAcreateFlashGCAfromFlashGCA(gca, TRs, fas, TEs, mri_in->nframes) ;
		GCAfree(&gca) ;
		gca = gca_tmp ;
		GCAhistoScaleImageIntensities(gca, mri_in) ;
	}
	else
		GCAhistoScaleImageIntensities(gca, mri_in) ;

  if (example_T1)
  {
    MRI *mri_T1, *mri_seg ;

    mri_seg = MRIread(example_segmentation) ;
    if (!mri_seg)
      ErrorExit(ERROR_NOFILE,"%s: could not read example segmentation from %s",
                Progname, example_segmentation) ;
    mri_T1 = MRIread(example_T1) ;
    if (!mri_T1)
      ErrorExit(ERROR_NOFILE,"%s: could not read example T1 from %s",
                Progname, example_T1) ;
    printf("scaling atlas intensities using specified examples...\n") ;
    MRIeraseBorderPlanes(mri_seg) ;
    GCArenormalizeToExample(gca, mri_seg, mri_T1) ;
    MRIfree(&mri_seg) ; MRIfree(&mri_T1) ;
  }

  if (tissue_parms_fname)   /* use FLASH forward model */
    GCArenormalizeToFlash(gca, tissue_parms_fname, mri_in) ;

  gcas = GCAfindAllSamples(gca, &nsamples, NULL) ;
  printf("using %d sample points...\n", nsamples) ;
  GCAcomputeSampleCoords(gca, mri_in, gcas, nsamples, transform) ;
  if (sample_fname)
    GCAtransformAndWriteSamples(gca, mri_in, gcas, nsamples, sample_fname, transform) ;
  

  for (n = 3 ; n <= nregions ; n++)
  {
    for (norm_samples = i = 0 ; i < NSTRUCTURES ; i++)
    {
      printf("finding control points in %s....\n", cma_label_to_name(normalization_structures[i])) ;
      gcas_struct = find_control_points(gca, gcas, nsamples, &struct_samples, n,
                                        normalization_structures[i], mri_in, transform, min_prior,
                                        ctl_point_pct) ;
			discard_unlikely_control_points(gca, gcas_struct, struct_samples, mri_in, transform,
																			cma_label_to_name(normalization_structures[i])) ;
      if (i)
      {
        GCA_SAMPLE *gcas_tmp ;
        gcas_tmp = gcas_concatenate(gcas_norm, gcas_struct, norm_samples, struct_samples) ;
        free(gcas_norm) ;
        norm_samples += struct_samples ;
        gcas_norm = gcas_tmp ;
      }
      else
      {
        gcas_norm = gcas_struct ; norm_samples = struct_samples ;
      }
    }

    printf("using %d total control points for intensity normalization...\n", norm_samples) ;
    if (normalized_transformed_sample_fname)
      GCAtransformAndWriteSamples(gca, mri_in, gcas_norm, norm_samples, 
                                  normalized_transformed_sample_fname, 
                                  transform) ;

    mri_norm = GCAnormalizeSamples(mri_in, gca, gcas_norm, file_only ? 0 :norm_samples,
                                   transform, ctl_point_fname) ;
    if (Gdiag & DIAG_WRITE)
    {
      char fname[STRLEN] ;
      sprintf(fname, "norm%d.mgh", n) ;
      printf("writing normalized volume to %s...\n", fname) ;
      MRIwrite(mri_norm, fname) ;
      sprintf(fname, "norm_samples%d.mgh", n) ;
      GCAtransformAndWriteSamples(gca, mri_in, gcas_norm, norm_samples, 
                                  fname, transform) ;

    }
    MRIcopy(mri_norm, mri_in) ;  /* for next pass through */
    MRIfree(&mri_norm) ;
  }

	for (input = 0 ; input < ninputs ; input++)
	{
		out_fname  = argv[3+ninputs+input] ;
	  printf("writing normalized volume to %s...\n", out_fname) ;	
		mri_in->tr = TRs[input] ; mri_in->flip_angle = fas[input] ; mri_in->te = TEs[input] ;
	  if (MRIwriteFrame(mri_in, out_fname, input)  != NO_ERROR)
	    ErrorExit(ERROR_BADFILE, "%s: could not write normalized volume to %s",
	              Progname, out_fname);
	}
  MRIfree(&mri_in) ;


#if 1
	printf("freeing GCA...") ;
  if (gca)
    GCAfree(&gca) ;
#endif
	printf("done.\n") ;
  if (mri_in)
    MRIfree(&mri_in) ;
  msec = TimerStop(&start) ;
  seconds = nint((float)msec/1000.0f) ;
  minutes = seconds / 60 ;
  seconds = seconds % 60 ;
  printf("normalization took %d minutes and %d seconds.\n", 
          minutes, seconds) ;
  if (diag_fp)
    fclose(diag_fp) ;
  exit(0) ;
  return(0) ;
}


/*----------------------------------------------------------------------
            Parameters:

           Description:
----------------------------------------------------------------------*/
static int
get_option(int argc, char *argv[])
{
  int  nargs = 0 ;
  char *option ;

  option = argv[1] + 1 ;            /* past '-' */
  StrUpper(option) ;
  if (!strcmp(option, "FSAMPLES"))
  {
    sample_fname = argv[2] ;
    nargs = 1 ;
    printf("writing control points to %s...\n", sample_fname) ;
  }
  else if (!strcmp(option, "MASK"))
  {
    mask_fname = argv[2] ;
    nargs = 1 ;
    printf("using MR volume %s to mask input volume...\n", mask_fname) ;
  }
  else if (!strcmp(option, "FONLY"))
  {
    ctl_point_fname = argv[2] ;
    nargs = 1 ;
		file_only = 1 ;
    printf("only using control points from file %s\n", ctl_point_fname) ;
  }
  else if (!strcmp(option, "DIAG"))
  {
    diag_fp = fopen(argv[2], "w") ;
    if (!diag_fp)
      ErrorExit(ERROR_NOFILE, "%s: could not open diag file %s for writing",
                Progname, argv[2]) ;
    printf("opening diag file %s for writing\n", argv[2]) ;
    nargs = 1 ;
  }
  else if (!strcmp(option, "DEBUG_VOXEL"))
  {
    Gx = atoi(argv[2]) ;
    Gy = atoi(argv[3]) ;
    Gz = atoi(argv[4]) ;
    printf("debugging voxel (%d, %d, %d)\n", Gx, Gy, Gz) ;
    nargs = 3 ;
  }
  else if (!strcmp(option, "DEBUG_NODE"))
  {
    Ggca_x = atoi(argv[2]) ;
    Ggca_y = atoi(argv[3]) ;
    Ggca_z = atoi(argv[4]) ;
    printf("debugging node (%d, %d, %d)\n", Ggca_x, Ggca_y, Ggca_z) ;
    nargs = 3 ;
  }
  else if (!strcmp(option, "TR"))
  {
    TR = atof(argv[2]) ;
    nargs = 1 ;
    printf("using TR=%2.1f msec\n", TR) ;
  }
  else if (!strcmp(option, "EXAMPLE"))
  {
    example_T1 = argv[2] ;
    example_segmentation = argv[3] ;
    printf("using %s and %s as example T1 and segmentations respectively.\n",
           example_T1, example_segmentation) ;
    nargs = 2 ;
  }
  else if (!strcmp(option, "TE"))
  {
    TE = atof(argv[2]) ;
    nargs = 1 ;
    printf("using TE=%2.1f msec\n", TE) ;
  }
  else if (!strcmp(option, "ALPHA"))
  {
    nargs = 1 ;
    alpha = RADIANS(atof(argv[2])) ;
    printf("using alpha=%2.0f degrees\n", DEGREES(alpha)) ;
  }
  else if (!strcmp(option, "NSAMPLES"))
  {
    normalized_transformed_sample_fname = argv[2] ;
    nargs = 1 ;
    printf("writing  transformed normalization control points to %s...\n", 
            normalized_transformed_sample_fname) ;
  }
  else if (!strcmp(option, "RENORM"))
  {
    renormalization_fname = argv[2] ;
    nargs = 1 ;
    printf("renormalizing using predicted intensity values in %s...\n",
           renormalization_fname) ;
  }
  else if (!strcmp(option, "FLASH"))
  {
    tissue_parms_fname = argv[2] ;
    nargs = 1 ;
    printf("using FLASH forward model and tissue parms in %s to predict"
           " intensity values...\n", tissue_parms_fname) ;
  }
  else if (!strcmp(option, "PRIOR"))
  {
    min_prior = atof(argv[2]) ;
    nargs = 1 ;
    printf("using prior threshold %2.2f\n", min_prior) ;
  }
  else if (!stricmp(option, "NOVAR"))
  {
    novar = 1 ;
    printf("not using variance estimates\n") ;
  }
  else switch (*option)
  {
  case 'W':
    Gdiag |= DIAG_WRITE ;
    break ;
  case 'N':
    nregions = atoi(argv[2]) ;
    printf("using %d regions/struct for normalization\n", nregions) ;
    nargs = 1 ;
    break ;
  case 'F':
    ctl_point_fname = argv[2] ;
    nargs = 1 ;
    printf("reading manually defined control points from %s\n", ctl_point_fname) ;
    break ;
  case 'V':
    Gdiag_no = atoi(argv[2]) ;
    nargs = 1 ;
    break ;
  case '?':
  case 'U':
		usage_exit(0) ;
    break ;
  case 'P':
    ctl_point_pct = atof(argv[2]) ;
    nargs = 1 ;
    printf("using top %2.1f%% wm points as control points....\n",
           100.0*ctl_point_pct) ;
    break ;
  default:
    printf("unknown option %s\n", argv[1]) ;
    exit(1) ;
    break ;
  }

  return(nargs) ;
}

static GCA_SAMPLE *
find_control_points(GCA *gca, GCA_SAMPLE *gcas_total,
                    int total_samples, int *pnorm_samples, int nregions, int label,
                    MRI *mri_in, TRANSFORM *transform, double min_prior, double ctl_point_pct)
{
  int        i, j, *ordered_indices, nsamples, xmin, ymin, zmin, xmax, ymax, zmax, xv,yv,zv,
             x, y, z, xi, yi, zi, region_samples, used_in_region, wsize=3, histo_peak, n,
             nbins ;
  GCA_SAMPLE *gcas, *gcas_region, *gcas_norm ;
  double     means[MAX_GCA_INPUTS], vars[MAX_GCA_INPUTS], val, nsigma ;
  HISTOGRAM  *histo, *hsmooth ;
  GC1D       *gc ;
	float      fmin, fmax ;
	MRI        *mri_T1 = NULL ;

#if 0
	{
		char fname[STRLEN] ;
		sprintf(fname, "%s/../T1", mri_in->fname) ;
		mri_T1 = MRIread(fname) ;
#if 0
		if (!mri_T1)
			ErrorExit(ERROR_NOFILE, "could not read T1 volume %s...", fname) ;
#endif
	}
#endif

	MRIvalRange(mri_in, &fmin, &fmax) ;
	nbins = (int)(fmax-fmin+1);
  histo = HISTOalloc(nbins) ; hsmooth = HISTOalloc(nbins) ;
  for (nsamples = i = 0 ; i < total_samples ; i++)
  {
    if (gcas_total[i].label != label)
      continue ;
    nsamples++ ;
  }

  *pnorm_samples = 0 ;
  printf("found %d control points for structure...\n", nsamples) ;
  gcas = (GCA_SAMPLE *)calloc(nsamples, sizeof(GCA_SAMPLE)) ;
  gcas_region = (GCA_SAMPLE *)calloc(nsamples, sizeof(GCA_SAMPLE)) ;
  gcas_norm = (GCA_SAMPLE *)calloc(nsamples, sizeof(GCA_SAMPLE)) ;
  if (!gcas || !gcas_region || !gcas_norm)
    ErrorExit(ERROR_NOMEMORY, "find_control_points: could not allocate %d samples\n",nsamples);

  for (j = i = 0 ; i < total_samples ; i++)
  {
    if (gcas_total[i].label != label)
      continue ;
    memmove(&gcas[j], &gcas_total[i], sizeof(GCA_SAMPLE)) ;
    j++ ;
  }
  ordered_indices = (int *)calloc(nsamples, sizeof(int)) ;

  gcas_bounding_box(gcas, nsamples, &xmin, &ymin, &zmin, &xmax, &ymax, &zmax, label) ;
  printf("bounding box (%d, %d, %d) --> (%d, %d, %d)\n",
         xmin, ymin, zmin, xmax, ymax, zmax) ;
  for (x = 0 ; x < nregions ; x++)
  {
    for (y = 0 ; y < nregions ; y++)
    {
      for (z = 0 ; z < nregions ; z++)
      {
        /* only process samples in this region */
				nsigma = 1.0 ;
				do
				{
					for (region_samples = i = 0 ; i < nsamples ; i++)
					{
						xi = (int)(nregions*(gcas[i].x - xmin) / (xmax-xmin+1)) ;
						yi = (int)(nregions*(gcas[i].y - ymin) / (ymax-ymin+1)) ;
						zi = (int)(nregions*(gcas[i].z - zmin) / (zmax-zmin+1)) ;
						if ((xi < 0 || xi >= nregions) ||
								(yi < 0 || yi >= nregions) ||
								(zi < 0 || zi >= nregions))
							DiagBreak() ;
						xv = gcas[i].x ; yv = gcas[i].y ; zv = gcas[i].z ;
						if (xi != x || yi != y || zi != z || gcas[i].prior < min_prior)
							continue ;
						
						if (xv == Gx && yv == Gy && zv == Gz)
							DiagBreak() ;
						if (sqrt(SQR(xv-Gx)+SQR(yv-Gy)+SQR(zv-Gz)) < 2)
							DiagBreak() ;
						if (min_region_prior(gca, gcas[i].xp, gcas[i].yp, gcas[i].zp, wsize, label) < min_prior)
							continue ;
						if (uniform_region(gca, mri_in, transform, xv, yv, zv, wsize, &gcas[i], nsigma) == 0)
							continue ;
						memmove(&gcas_region[region_samples], &gcas[i], sizeof(GCA_SAMPLE)) ;
						region_samples++ ;
						if (gcas[i].x == Gx && gcas[i].y == Gy && gcas[i].z == Gz)
							DiagBreak() ;
					}
					nsigma *= 1.1 ;
				} while (region_samples < 8 && nsigma < 3) ;
					
				if (region_samples < 8)    /* can't reliably estimate statistics */
					continue ;
				if (DIAG_VERBOSE_ON)
					printf("\t%d total samples found in region (%d, %d, %d)\n", 
								 region_samples,x, y,z) ;
        /* compute mean and variance of label within this region */
				for (n = 0 ; n < gca->ninputs ; n++)
				{
					HISTOclear(histo, histo) ;
					histo->bin_size = 1 ;
					for (means[n] = vars[n] = 0.0, i = 0 ; i < region_samples ; i++)
					{
						MRIsampleVolumeFrame(mri_in, gcas_region[i].x,gcas_region[i].y,gcas_region[i].z, n, &val) ;
						histo->counts[(int)val]++ ;
						means[n] += val ;
						vars[n] += (val*val) ;
#if 0
						if (mri_T1)
						{
							val = MRIvox(mri_T1, gcas_region[i].x,gcas_region[i].y,gcas_region[i].z) ;
							if (val < 85 || val > 130)
							{
								FILE *fp ;
								fp = fopen("badpoints.log", "a") ;
								fprintf(fp, "%s: (%d, %d, %d): %f\n",
												mri_in->fname, (int)gcas_region[i].x,(int)gcas_region[i].y,(int)gcas_region[i].z,val) ;
								fclose(fp) ;
								printf("!!!!!!!!!!!!!!!!!!!!!!! %s: (%d, %d, %d): %f !!!!!!!!!!!!!!!!!!!!!!!!!!!\n",
											 mri_in->fname, (int)gcas_region[i].x,(int)gcas_region[i].y,(int)gcas_region[i].z,val) ;
							}
						}
#endif
					}
					
					HISTOsmooth(histo, hsmooth, 2) ;
					histo_peak = HISTOfindHighestPeakInRegion(hsmooth, 1, hsmooth->nbins) ;
					if (histo_peak < 0)   /* couldn't find a valid peak? */
						break ;

					for (means[n] = vars[n] = 0.0, i = 0 ; i < region_samples ; i++)
					{
						MRIsampleVolumeFrame(mri_in, gcas_region[i].x,gcas_region[i].y,gcas_region[i].z, n, &val) ;
						means[n] += val ;
						vars[n] += (val*val) ;
					}
					means[n] /= (double)region_samples ;
					vars[n] = vars[n] / (double)region_samples - means[n]*means[n] ;

					means[n] = histo_peak ;
					if (DIAG_VERBOSE_ON)
						printf("\tlabel %s[%d]: %2.1f +- %2.1f\n", cma_label_to_name(label), n, means[n], sqrt(vars[n])) ;
				}
				
				/* ignore GCA mean and variance - use image instead (otherwise bias field will mess us up) */
				for (i = 0 ; i < region_samples ; i++)
				{
					int r ;
					
					for (r = 0 ; r < gca->ninputs ; r++)
						gcas_region[i].means[r] = means[r] ;
					/*          gcas_region[i].var = var ;*/
				}

				GCAcomputeLogSampleProbability(gca, gcas_region, mri_in, transform, region_samples) ;
				GCArankSamples(gca, gcas_region, region_samples, ordered_indices) ;
#if 0
				/* use detected peak as normalization value for whole region */
				used_in_region = 1 ; j = ordered_indices[0] ;
				MRIvox(mri_in, gcas_region[j].x, gcas_region[j].y, gcas_region[j].z) = histo_peak ;
				memmove(&gcas_norm[*pnorm_samples], &gcas_region[j], sizeof(GCA_SAMPLE)) ;
				(*pnorm_samples)++ ;
#else
#if 1
				GCAremoveOutlyingSamples(gca, gcas_region, mri_in, transform, region_samples, 2.0) ;
#endif
        for (used_in_region = i = 0 ; i < region_samples ; i++)
        {
          j = ordered_indices[i] ;
          if (gcas_region[j].label != label)  /* it was an outlier */
            continue ;
          memmove(&gcas_norm[*pnorm_samples], &gcas_region[j], sizeof(GCA_SAMPLE)) ;
          (*pnorm_samples)++ ; used_in_region++ ;
        }
        if ((used_in_region <= 0) && region_samples>0)
        {
          j = ordered_indices[0] ;
          /*          gcas_region[j].label = label ;*/
          printf("forcing use of sample %d @ (%d, %d, %d)\n", j,
                 gcas_region[j].x, gcas_region[j].y, gcas_region[j].z) ;
          memmove(&gcas_norm[*pnorm_samples], &gcas_region[j], sizeof(GCA_SAMPLE)) ;
          (*pnorm_samples)++ ; used_in_region++ ;
        }
#endif
				if (DIAG_VERBOSE_ON)
					printf("\t%d samples used in region\n", used_in_region) ;
      }
    }
  }

  /* put gca means back into samples */
  for (i = 0 ; i < *pnorm_samples ; i++)
  {
    gc = GCAfindPriorGC(gca, gcas_norm[i].xp, gcas_norm[i].yp, gcas_norm[i].zp,
                        gcas_norm[i].label) ; 
    if (gc)
    {
			int r, c, v ;

			for (v = r = 0 ; r < gca->ninputs ; r++)
			{
				for (c = r ; c < gca->ninputs ; c++, v++)
				{
					gcas_norm[i].means[v] = gc->means[v] ;
					gcas_norm[i].covars[v] = gc->covars[v] ;
				}
			}
    }
  }
  HISTOfree(&histo) ; HISTOfree(&hsmooth) ;
  free(gcas_region) ;
  free(gcas) ;
	if (mri_T1)
		MRIfree(&mri_T1) ;
  return(gcas_norm) ;
}

static GCA_SAMPLE *
gcas_concatenate(GCA_SAMPLE *gcas1, GCA_SAMPLE *gcas2, int n1, int n2)
{
  GCA_SAMPLE *gcas ;
  int        i ;

  gcas = (GCA_SAMPLE *)calloc(n1+n2, sizeof(GCA_SAMPLE)) ;
  if (!gcas)
    ErrorExit(ERROR_NOMEMORY, "gcas_concatenate: could not allocate %d samples",n1+n2) ;

  for (i = 0 ; i < n1 ; i++)
    memmove(&gcas[i], &gcas1[i], sizeof(GCA_SAMPLE)) ;
  for (i = 0 ; i < n2 ; i++)
    memmove(&gcas[i+n1], &gcas2[i], sizeof(GCA_SAMPLE)) ;

  return(gcas) ;
}

static int
gcas_bounding_box(GCA_SAMPLE *gcas, int nsamples, int *pxmin, int *pymin, int *pzmin, 
                  int *pxmax, int *pymax, int *pzmax, int label)
{
  int   i, xmin, ymin, zmin, xmax, ymax, zmax ;


  xmax = ymax = zmax = -1 ;
  xmin = ymin = zmin = 1000000 ;
  for (i = 0 ; i < nsamples ; i++)
  {
    if (gcas[i].x < xmin)
      xmin = gcas[i].x ;
    if (gcas[i].y < ymin)
      ymin = gcas[i].y ;
    if (gcas[i].z < zmin)
      zmin = gcas[i].z ;

    if (gcas[i].x > xmax)
      xmax = gcas[i].x ;
    if (gcas[i].y > ymax)
      ymax = gcas[i].y ;
    if (gcas[i].z > zmax)
      zmax = gcas[i].z ;
  }

  *pxmin = xmin ; *pymin = ymin ; *pzmin = zmin ;
  *pxmax = xmax ; *pymax = ymax ; *pzmax = zmax ;
  return(NO_ERROR) ;
}

static double
min_region_prior(GCA *gca, int xp, int yp, int zp, int wsize, int label)
{
  int       whalf, xi, yi, zi, xk, yk, zk ;
  double    min_prior, prior ;
  GCA_PRIOR *gcap ;

  min_prior = 1.0 ; whalf = (wsize-1)/2 ;
  for (xi = -whalf ; xi <= whalf ; xi++)
  {
    xk = xp+xi ;
    if (xk < 0 || xk >= gca->prior_width)
      continue ;
    for (yi = -whalf ; yi <= whalf ; yi++)
    {
      yk = yp+yi ;
      if (yk < 0 || yk >= gca->prior_height)
        continue ;
      for (zi = -whalf ; zi <= whalf ; zi++)
      {
        zk = zp+zi ;
        if (zk < 0 || zk >= gca->prior_depth)
          continue ;
        gcap = &gca->priors[xk][yk][zk] ;
        prior = getPrior(gcap, label) ;
        if (prior < min_prior)
          min_prior = prior ;
      }
    }
  }

  return(min_prior) ;
}

static int
uniform_region(GCA *gca, MRI *mri, TRANSFORM *transform, int x, int y, int z, int wsize, GCA_SAMPLE *gcas,
							 float nsigma)
{
  int   xk, yk, zk, whalf, xi, yi, zi, n ;
  Real   val0, val, sigma, min_val,max_val, thresh ;
	MATRIX *m ;
	GC1D   *gc ;
	
	gc = GCAfindSourceGC(gca, mri, transform, x, y, z, gcas->label) ;
	if (!gc)
		return(0) ;
	m = load_covariance_matrix(gc, NULL, gca->ninputs) ;

  whalf = (wsize-1)/2 ;
	for (n = 0 ; n < gca->ninputs ; n++)
	{
		sigma = sqrt(*MATRIX_RELT(m, n+1, n+1)) ;
		MRIsampleVolumeFrame(mri, (Real)x, (Real)y, (Real)z, n, &val0) ;
		if (sigma < 0.05*val0)   /* don't let it be too small */
			sigma = 0.05*val0 ;
		if (sigma > 0.1*val0)    /* don't let it be too big */
			sigma = 0.1*val0 ;
		min_val = max_val = val0 ;
		thresh = nsigma*sigma ;
		
		for (xk = -whalf ; xk <= whalf ; xk++)
		{
			xi = mri->xi[x+xk] ;
			for (yk = -whalf ; yk <= whalf ; yk++)
			{
				yi = mri->yi[y+yk] ;
				for (zk = -whalf ; zk <= whalf ; zk++)
				{
					zi = mri->zi[z+zk] ;
					MRIsampleVolumeFrame(mri, (Real)xi, (Real)yi, (Real)zi, n, &val) ;
					if (val < min_val)
						min_val = val ;
					if (val > max_val)
						max_val = val ;
					if (fabs(val-val0) > thresh || fabs(max_val-min_val) > thresh)
						return(0) ;
				}
			}
		}
	}
  
	MatrixFree(&m) ;
  return(1) ;
}

static int
discard_unlikely_control_points(GCA *gca, GCA_SAMPLE *gcas, int nsamples, 
																MRI *mri_in, TRANSFORM *transform, char *name)
{
	int    i, xv, yv, zv, n, peak, start, end, num ;
	HISTO *h, *hsmooth ;
	float  fmin, fmax ;
	Real   val ;

	for (num = n = 0 ; n < gca->ninputs ; n++)
	{
		MRIvalRangeFrame(mri_in, &fmin, &fmax, n) ;
		h = HISTOalloc(nint(fmax-fmin)+1) ;
		for (i = 0 ; i < nsamples ; i++)
		{
			xv = gcas[i].x ; yv = gcas[i].y ; zv = gcas[i].z ;
			if (xv == Gx && yv == Gy && zv == Gz)
				DiagBreak() ;
			MRIsampleVolumeFrame(mri_in, gcas[i].x,gcas[i].y,gcas[i].z, n, &val) ;
			h->counts[nint(val-fmin)]++ ;
		}

		hsmooth = HISTOsmooth(h, NULL, 2) ;
		peak = HISTOfindHighestPeakInRegion(hsmooth, 0, h->nbins-1) ;
		end = HISTOfindEndOfPeak(hsmooth, peak, 0.01) ;
		start = HISTOfindStartOfPeak(hsmooth, peak, 0.01) ;
		printf("%s: limiting intensities to %2.1f --> %2.1f\n", name, fmin+start, fmin+end) ;
		for (i = 0 ; i < nsamples ; i++)
		{
			xv = gcas[i].x ; yv = gcas[i].y ; zv = gcas[i].z ;
			if (xv == Gx && yv == Gy && zv == Gz)
				DiagBreak() ;
			MRIsampleVolumeFrame(mri_in, gcas[i].x,gcas[i].y,gcas[i].z, n, &val) ;
			if (val-fmin < start || val-fmin > end)
			{
				num++ ; gcas[i].label = 0 ;
			}
		}
		HISTOfree(&h) ; HISTOfree(&hsmooth) ;
	}

	printf("%d of %d (%2.1f%%) samples deleted\n", num, nsamples, 100.0f*(float)num/(float)nsamples) ;
	return(NO_ERROR) ;
}

static void
usage_exit(int code)
{
    printf("usage: %s <in volume> <atlas> <transform> <normalized volume>\n\n", 
           Progname) ;
    printf("\t-fsamples <filename>         write control points to filename\n");
    printf("\t-nsamples <filename>         write transformed normalization control points to filename\n");
    printf("\t-mask <mri_vol>              use mri_vol to mask input\n");
    printf("\t-f <filename>                define control points from filename\n");
    printf("\t-fonly <filename>            only use control points from filename\n");
    printf("\t-diag <filename>             write to log file\n");
    printf("\t-debug_voxel <x> <y> <z>     debug voxel\n");
    printf("\t-debug_node <x> <y> <z>      debug node\n");
    printf("\t-tr <float n>                set TR in msec\n");
    printf("\t-te <float n>                set TE in msec\n");
    printf("\t-alpha <float n>             set alpha in radians\n");
    printf("\t-example <mri_vol> <segmentation> use T1 (mri_vol) and segmentation as example\n");
    printf("\t-novar                       do not use variance estimates\n");
    printf("\t-renorm <mri_vol>            renormalize using predicted intensity values in mri_vol\n");
    printf("\t-flash                       use FLASH forward model to predict intensity values\n");
    printf("\t-prior <float t>             use prior threshold t (default=.6)\n");
    printf("\t-w                           write normalized volume each nregion iteration to norm(n).mgh(see -n)\n");
    printf("\t-n <int n>                   use n regions/struct for normalization\n");
    printf("\t-v <int n>                   does nothing as far as i can tell, but an option\n");
    printf("\t-p <float p>                 use top p percent(default=.25) white matter points as control points\n"); 
    exit(code) ;
}
