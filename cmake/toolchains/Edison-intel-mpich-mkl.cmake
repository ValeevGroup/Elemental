#       
# module load cray-mpich/7* (any 7+ version is ok)
# module load gcc/4.9.2 && module load PrgEnv-intel
#

# The Cray wrappers
set(COMPILER_DIR $ENV{CRAYPE_DIR}/bin)
set(CMAKE_C_COMPILER       ${COMPILER_DIR}/cc)
set(CMAKE_CXX_COMPILER     ${COMPILER_DIR}/CC)
set(CMAKE_Fortran_COMPILER ${COMPILER_DIR}/ftn)

# This is just a hack, as this machine always uses the above wrappers
set(MPI_C_COMPILER ${CMAKE_C_COMPILER})
set(MPI_CXX_COMPILER ${CMAKE_CXX_COMPILER})
set(MPI_Fortran_COMPILER ${CMAKE_Fortran_COMPILER})

if(CMAKE_BUILD_TYPE MATCHES PureDebug OR
   CMAKE_BUILD_TYPE MATCHES HybridDebug)
  set(C_FLAGS   "-g  -static -Wl,-Bstatic")
  set(CXX_FLAGS "-g  -static -Wl,-Bstatic")
else()
  set(C_FLAGS   "-O3 -static -Wl,-Bstatic")
  set(CXX_FLAGS "-O3 -static -Wl,-Bstatic")
endif()

set(OpenMP_CXX_FLAGS "-openmp")

# We are using ScaLAPACK from MKL, which is compiled against Intel MPI.
# We leverage ABI compatibility (http://www.mpich.org/abi/) between
# Intel MPI 5 and Cray MPI 7 and hope for the best.
# A more robust solution will be available in the future.
set(MATH_LIBS "-mkl=cluster")
set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")
