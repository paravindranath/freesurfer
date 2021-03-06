##
## Makefile.am 
##

#######################################################################
# When you create your own program directory, copy the 'from' to the 
# 'to' lines to your Makefile.am and change dummy to your program name.
#  
# 0. mkdir  dev/yourdir and put .c file in there
# 1. modify dev/configure.in to have yourdir/Makefile
# 2. modify dev/Makefile.am to have yourdir as one of SUBDIRS
# 3. create dev/yourdir/Makefile.am  (follow dummy/Makefile.am)
#      i.e. replace dummy with your program name.
#
# 4. ./setup_configure at the top directory, i.e. dev/ (which will create 
#    Makefile.in in yourdir directory and configure from configure.in)
# 5. ./configure .... and make to make sure that your new thing is working
# 6. try make to make sure that yourthing is compiled.
#
# After confirming everything working:
# 7. checkin yourdir/Makefile.am, yourdir/yourprog.c or cpp.  
#    (Don't checkin Makefile or Makefile.in.)
# 8. commit dev/configure.in, dev/Makefile.am
#
# 9. lastly, delete this help text from your own Makefile.am!
#
# If you have problems, please let us know (zkaufman@nmr.mgh.harvard.edu
# or nicks@nmr.mgh.harvard.edu).
#########################################################################
# from ------------------------------------------------

AM_CFLAGS=-I$(top_srcdir)/include
AM_CXXFLAGS=-I$(top_srcdir)/include $(VXL_CFLAGS) $(VTK_CFLAGS) $(ITK_CFLAGS) -I$(top_srcdir)/anatomicuts/Code
bin_PROGRAMS =AnatomiCuts 
AnatomiCuts_SOURCES=AnatomiCuts.cxx \
		Code/FilterFibers.txx			 Code/itkFiber.txx			      Code/itkLabelPerPointVariableLengthVector.txx		  Code/itkLabelsPointToPointMembershipFunction.txx  Code/itkVTKPolyDataToPolylineMeshFilter.txx \
Code/HierarchicalClusteringPruner.txx	 Code/itkFixedVTKSamplingFilter.txx	      Code/itkLabelsEntropyAndIntersectionMembershipFunction.txx  Code/itkLabelVariableLengthVector.txx		    Code/NormalizedCutsFilter.txx \ 
Code/itkAppendBundleFilter.txx		 Code/itkLabelMapVariableLengthVector.txx     Code/itkLabelsEntropyMembershipFunction.txx		  Code/itkPolylineCell.txx			    Code/ThreadedMembershipFunction.txx \
Code/itkEuclideanMembershipFunction.txx  Code/itkLabelPerPointMembershipFunction.txx  Code/itkLabelsHistogramMembershipFunction.txx		  Code/itkPolylineMeshToVTKPolyDataFilter.txx	    Code/TrkVTKPolyDataFilter.txx 
AnatomiCuts_LDADD=$(addprefix $(top_builddir)/, $(LIBS_MGH)) \
 $(LIB_LAPACK) $(LIB_BLAS) $(LIB_G2C_A) $(LIB_GFORTRAN)  \
 $(VTK_LIBS) -lvtkIO -lvtkCommon -lvtkFiltering -lvtkSys \
 ${ITK_LIBS} -llibITKIOImageBase -llibITKIO -llibITKAlgorithms -llibITKCommon -llibITKNumerics -llibITKMetaIO -llibITKniftiio -llibITKNrrdIO -llibitkpng \
-llibitksys -llibitktiff -llibitkv3p_netlib -llibitkzlib -llibitkgdcm -llibitkopenjpeg -llibitkjpeg8 -llibitkjpeg12 -llibitkjpeg16 -llibITKDICOMParser
AnatomiCuts_LDFLAGS=$(OS_LDFLAGS)

# to --------------------------------------------------
#
# Where $(OS_LDFLAGS) is to compile programs statically for Linux.
#
# You can have multiple bin_PROGRAMS in the same directory.
#
########################################################################
# When you want to create your program to test, use the following 
# framework:
# You can have as many programs (separated by space) in check_PROGRAMS.
# Your program can consist of many sources (you add to _SOURCES= line).
# You can do "make myown" to compile. The default CFLAGS="-g -O2", but
# you can do "make CFLAGS=-g myown" to create a debugging version.
#
# The program listed as check_PROGRAMS are not compiled under standard 
# "make". Only "make check" will compile them. See mri_convert's
# Makefile.am for an example of its 'make check' test setup.
# 
# Please don't remove myown.c from this dummy directory.
########################################################################
#check_PROGRAMS=myown

#myown_SOURCES=myown.c
#myown_LDADD=$(addprefix $(top_builddir)/, $(LIBS_MGH))

# Our release target. Include files to be excluded here. They will be
# found and removed after 'make install' is run during the 'make
# release' target.
EXCLUDE_FILES=
include $(top_srcdir)/Makefile.extra
