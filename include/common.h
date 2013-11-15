/************************************************************************
 * Copyright (C) 2011-2013:
 *
 *	Edgardo Mejia-Roa(*), Carlos Garcia, Jose Ignacio Gomez,
 *	Manuel Prieto, Francisco Tirado and Alberto Pascual-Montano(**).
 *
 *	(*)  ArTeCS Group, Complutense University of Madrid (UCM), Spain.
 *	(**) Functional Bioinformatics Group, Biocomputing Unit,
 *		National Center for Biotechnology-CSIC, Madrid, Spain.
 *
 *	E-mail for E. Mejia-Roa: <edgardomejia@fis.ucm.es>
 *	E-mail for A. Pascual-Montano: <pascual@cnb.csic.es>
 *
 *
 * This file is part of bioNMF-mGPU..
 *
 * BioNMF-mGPU is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * BioNMF-mGPU is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with BioNMF-mGPU.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/
/**********************************************************
 * common.h
 *	Some generic definitions, constants, macros and functions used by bioNMF-mGPU.
 *
 * NOTE: The following macro constants can be defined to modify the
 *	behavior of routines, as well as some constant and data-type definitions.
 *
 *	Debug / Testing:
 *		NMFGPU_FIXED_INIT: Uses "random" values generated from the fixed seed: FIXED_SEED.
 *
 **********************************************************
 **********************************************************
 **********************************************************
 *
 * Data matrices:
 * 	V (N rows, M columns): Input matrix,
 * 	K: Factorization Rank,
 * 	W (N,K): Output matrix,
 * 	H (K,M): Output matrix,
 * so that V ~ W*H
 *
 * NOTE: In order to improve performance:
 *	- Matrix H is stored in memory as COLUMN-major (i.e., it is transposed).
 *
 *	- All matrices include useless data for padding. Padded dimensions
 *	  are denoted with the 'p' character, e.g., 'Mp' (i.e.,, M + padding)
 *	  or 'Kp' (factorization_rank + padding).
 *
 *	- Padded dimensions are a multiple of memory_alignment
 *	  (a global variable which currently is equal to warpSize or warpSize/2).
 *
 ***************
 *
 * Multi-GPU version:
 *
 * When the input matrix V is distributed among multiple devices each host thread processes
 * the following sets of rows and columns:
 *	Vrow[ 1..NnP ][ 1..M ] <-- V[ bN..(bN+NnP) ][ 1..M ]	(i.e., NnP rows, starting from bN)
 *	Vcol[ 1..N ][ 1..MnP ] <-- V[ 1..N ][ bM..(bM+MnP) ]	(i.e., MnP columns, starting from bM)
 *
 * Such sets allow to update the corresponding rows and columns of W and H, respectively.
 *
 * Note that each host thread has a private copy of matrices W and H, which must be synchronized
 * after being updated.
 *
 ****************
 *
 * Large input matrix (blockwise processing):
 *
 * If the input matrix (or the portion assigned to this device) is too large for the GPU memory,
 * it must be blockwise processed as follow:
 *	d_Vrow[1..BLN][1..Mp] <-- Vrow[ offset..(offset + BLN) ][1..Mp]			(i.e., BLN <= NnP rows)
 *	d_Vcol[1..N][1..BLMp] <-- Vcol[1..N][ offset_Vcol..(offset_Vcol + BLMp) ]	(i.e., BLM <= MnP columns)
 *
 * Note that padded dimensions are denoted with the suffix 'p' (e.g., Mp, BLMp, etc).
 *
 * In any case, matrices W and H are fully loaded into the GPU memory.
 *
 * Information for blockwise processing is stored in two block_t structures (one for each dimension).
 * Such structures ('block_N' and 'block_M') are initialized in init_block_conf() routine.
 *
 ****************
 *
 * WARNING:
 *	- Requires support for ISO-C99 standard. It can be enabled with 'gcc -std=c99'.
 *
 *********************************************************/

#if ! NMFGPU_COMMON_H
#define NMFGPU_COMMON_H (1)

///////////////////////////////////////////////////////

/* Always process this header as C code, not C++. */
#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////

#include <stdbool.h>

#include "index_type.h"

// ---------------------------------------------
// ---------------------------------------------

/* Constants */

// Default values for some parameters.
#ifndef DEFAULT_K
	#define DEFAULT_K 2
#endif

#ifndef DEFAULT_NITERS
	#define DEFAULT_NITERS 2000
#endif

#ifndef DEFAULT_NITER_CONV
	#define DEFAULT_NITER_CONV 10
#endif

#ifndef DEFAULT_STOP_THRESHOLD
	#define DEFAULT_STOP_THRESHOLD 40
#endif

#ifndef DEFAULT_GPU_DEVICE
	#define DEFAULT_GPU_DEVICE 0
#endif


// Fixed seed for the random-values generator.
#ifndef FIXED_SEED
	#define FIXED_SEED 3
#endif

// ---------------------------------------------

// Selects the appropriate "restrict" keyword.

#undef RESTRICT

#if defined(__CUDACC__)			/* CUDA source code */
	#define RESTRICT __restrict__
#else					/* C99 source code */
	#define RESTRICT restrict
#endif

// ---------------------------------------------

/* Data types */

/* Structure for arguments */

struct input_arguments {
	char const *restrict filename;
	bool numeric_hdrs;		// Input matrix has numeric columns headers (ignored for binary files).
	bool numeric_lbls;		// Input matrix has numeric row labels (ignored for binary files).

	index_t is_bin;			// Input file is binary.
					// == 1 for non-native format (i.e., double-precision data, and "unsigned int" for dimensions).
					// > 1 for native format (i.e., the compiled types for matrix data and dimensions).

	index_t save_bin;		// Saves output matrices to binary files.
					// == 1 for non-native format (i.e., double-precision data, and "unsigned int" for dimensions).
					// > 1 for native format (i.e., the compiled types for matrix data and dimensions).

	index_t k;			// Starting factorization rank.
	index_t nIters;			// Maximum number of iterations per run.
	index_t niter_test_conv;	// Number of iterations before testing convergence.
	index_t stop_threshold;		// Stopping criterion.

	index_t gpu_device;		// GPU device ID (NMF_[m]GPU only).

	index_t idx_other_args;		// Index in argv[] with additional executable-specific arguments.

};

//////////////////////////////////////////////////
//////////////////////////////////////////////////

/*
 * Prints to the specified file all arguments regarding
 * the input matrix (e.g., matrix dimensions and format).
 */
void help_matrix( FILE *restrict file );

// -------------------------------------

/*
 * Prints to the specified file all arguments regarding
 * the NMF algorithm (e.g., factorization rank, number of iterations, etc).
 */
void help_nmf( FILE *restrict file );

// -------------------------------------

/*
 * Prints to the specified file all arguments regarding
 * bioNMF-mGPU main program (single or multi-GPU version).
 */
void print_nmf_gpu_help( char const *restrict const execname, FILE *restrict file );

// -------------------------------------

/*
 * Checks all arguments.
 *
 * If verbose_error is 'true', shows error messages.
 *
 * Sets 'help' to 'true' if help message was requested ('-h' or '-H' options).
 *
 * Returns EXIT_SUCCESS or EXIT_FAILURE.
 */
int check_arguments( int argc, char const *restrict *restrict argv, bool verbose_error, bool *restrict help,
			struct input_arguments *restrict arguments );

// -------------------------------------

/*
 * Gets the difference between classification and last_classification vectors
 */
index_t get_difference( index_t const *restrict classification, index_t const *restrict last_classification, index_t m );

// -------------------------------------

/*
 * Retrieves a "random" value that can be used as seed.
 */
index_t get_seed( void );

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

#undef RESTRICT	/* To select the appropriate "restrict" keyword. */

///////////////////////////////////////////////////////

/* Always process this header as C code, not C++. */
#ifdef __cplusplus
}
#endif

///////////////////////////////////////////////////////

#endif /* NMFGPU_COMMON_H */
