/*
 *  Author: Paul Horton
 *  Copyright: Paul Horton 2021, All rights reserved.
 *  Created: 20211201
 *  Updated: 20240603
 *  Licence: GPLv3
 *  Description: Simple demonstration of a Bayesian way to guess at the number of components
 *               behind a sample of numerical data.
 *  Compile:  gcc -O3 -o Gaussian_poolOrNot Gaussian_poolOrNot.c GSLfun.c -lgsl -lgslcblas -lm
 *  Environment: $GSL_RNG_SEED
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "GSLfun.h"
/* ───────────  Global definitions and variables  ────────── */
#define DATA_N 40
#define CDF_GAUSS_N 20
#define CDF_GAMMA_N 10
#define CDF_JBETA_N 40

typedef struct{
  double mixCof;
  Gauss_params Gauss1;
  Gauss_params Gauss2;
} Gauss_mixture_params;


const Gauss_params mu_prior_params= {0.0, 4.0};
const double sigma_prior_param_a= 0.5;
const double sigma_prior_param_b= 2.0;


double data[DATA_N];
const uint dataN= DATA_N;

enum modelNames{ POOLED, DIFFER };

const uint sampleRepeatNum= 2000000;



/* ───────────  Functions to help summarize or dump the data  ────────── */

double data_sample_mean(){
  double mean= 0.0;
  for( uint i= 0;  i < dataN;  ++i ){
    mean += data[i];
  }
  return  mean / (double) dataN;
}

double data_sample_variance(){
  double mean= data_sample_mean();
  double var= 0.0;
  for( uint i= 0;  i < dataN;  ++i ){
    double diff=  data[i] - mean;
    var +=  diff * diff;
  }
  return  var / (double) (dataN);
}


// ternary CMP function for use with qsort
int CMPdata( const void *arg1, const void *arg2 ){
  return(
         (*(double*) arg1 < *(double*) arg2)? -1 :
         (*(double*) arg2 < *(double*) arg1)? +1 :
         /* else    *arg1 == *arg2   */        0);
}

void data_print(){
  qsort(  data,  dataN,  sizeof(double), CMPdata  );
  for( uint i= 0;  i < dataN;  ++i ){
    printf( "%+5.3f ", i, data[i] );
  }
}


/* ───────────  Functions used for sampling/generating data   ────────── */

Gauss_params prior_Gauss_params_sample(){
  Gauss_params params;
  params.mu=   GSLfun_ran_gaussian( mu_prior_params );
  params.sigma=  sigma_of_precision( GSLfun_ran_gamma(sigma_prior_param_a, sigma_prior_param_b) );
  return  params;
}


Gauss_mixture_params prior_Gauss_mixture_params_sample(){
  Gauss_mixture_params params;
  params.mixCof=  GSLfun_ran_beta_Jeffreys();
  params.Gauss1=  prior_Gauss_params_sample();
  params.Gauss2=  prior_Gauss_params_sample();
  return  params;
}

void data_generate_1component( Gauss_params params ){
  for( uint i= 0; i < dataN; ++i ){
    data[i]=  GSLfun_ran_gaussian( params );
  }
}

void data_generate_2component( Gauss_mixture_params params ){
  for( uint i= 0; i < dataN; ++i ){
    data[i]=  GSLfun_ran_gaussian
      (gsl_ran_flat01() < params.mixCof?  params.Gauss1  : params.Gauss2);
  }
}


/* ───────────  Functions used for numerical integration  ────────── */

// Arrays to hold precomputed values.
double cdfInv_Gauss[CDF_GAUSS_N];  const double cdf_Gauss_n= CDF_GAUSS_N;
double cdfInv_gamma[CDF_GAMMA_N];  const double cdf_gamma_n= CDF_GAMMA_N;
double cdfInv_JBeta[CDF_JBETA_N];  const double cdf_JBeta_n= CDF_JBETA_N;

//  Precompute the cumulative probabilities of μ and σ discrete values.
//  The probabilities depend on the current prior_params values
void cdfInv_precompute(){
  double x;
  // Since Normal range is unbounded, precompute cdfInv for vals:  ¹⁄₍ₙ₊₁₎...ⁿ⁄₍ₙ₊₁₎
  for(  uint i= 0; i < cdf_Gauss_n; ++i  ){
    x= (i+1) / (double) (1+cdf_Gauss_n);
    cdfInv_Gauss[i]=  gsl_cdf_gaussian_Pinv( x, mu_prior_params.sigma );
  }
  for(  uint i= 0; i < cdf_gamma_n; ++i  ){
    x= i / (double) (cdf_gamma_n);
    cdfInv_gamma[i]=  gsl_cdf_gamma_Pinv( x, sigma_prior_param_a, sigma_prior_param_b );
    //printf( "cdfInv_Gamma[%u]= %g\n", i, cdfInv_gamma[i] );
  }
  for(  uint i= 0; i < cdf_JBeta_n; ++i  ){
    // By symmetry, only need Beta values for p ≦ 0.5.  For example p=0.8, is the same p=0.2 with Gauss components swapped.
    x= 0.5 * i / (double) (cdf_JBeta_n);
    cdfInv_JBeta[i]=  gsl_cdf_beta_Pinv( x, 0.5, 0.5 );
    //printf( "cdfInv_JBeta[%u]= %g\n", i, cdfInv_JBeta[i] );
  }
}



/* Compute Riemann sum to approximate the integral
 *
 * ∫ μ,σ  P[D,μ,σ]
 *
*/
double data_prob_1component_bySumming(){
  double prob_total= 0.0;
  for(  uint m= 0;  m < cdf_Gauss_n;  ++m  ){
    double mu= cdfInv_Gauss[m];
    for(  uint s= 0;  s < cdf_gamma_n;  ++s  ){
      double sigma=  sigma_of_precision( cdfInv_gamma[s] );
      Gauss_params cur_params= {mu, sigma};
      double curProb= 1.0;
      for(  uint d= 0;  d < dataN;  ++d  ){
        double newProb= GSLfun_ran_gaussian_pdf( data[d], cur_params );
        curProb *= newProb;
      }
      prob_total += curProb;
    }
  }
  return  prob_total / (double) (cdf_Gauss_n * cdf_gamma_n);
}


/* Compute Riemann sum to approximate integral
 *
 * ∫ m,μ₁,σ₁,μ₂,σ₂  P[D,m,μ₁,σ₁,μ₂,σ₂]
 *
*/
double data_prob_2component_bySumming(){
  double prob_total= 0.0;

  for(  uint m1= 0;  m1 < cdf_Gauss_n;  ++m1  ){
    double mu1= cdfInv_Gauss[m1];
    for(  uint m2= 0;  m2 < cdf_Gauss_n;  ++m2  ){
      double mu2= cdfInv_Gauss[m2];
      for(  uint s1= 0;  s1 < cdf_gamma_n;  ++s1  ){
        double sigma1=  sigma_of_precision( cdfInv_gamma[s1] );
        Gauss_params cur_params1= {mu1, sigma1};
        for(  uint s2= 0;  s2 < cdf_gamma_n;  ++s2  ){
          double sigma2=  sigma_of_precision( cdfInv_gamma[s2] );
          Gauss_params cur_params2= {mu2, sigma2};
          for(  uint mi= 0;  mi < cdf_JBeta_n;  ++mi  ){
            double mixCof= cdfInv_JBeta[mi];
            double curProb= 1.0;
            for(  uint d= 0;  d < dataN;  ++d  ){
              double newProb=  mixCof  * GSLfun_ran_gaussian_pdf( data[d], cur_params1 )
                +           (1-mixCof) * GSLfun_ran_gaussian_pdf( data[d], cur_params2 );
              curProb *= newProb;
            }
            prob_total += curProb;
          }
        }
      }
    }
  }
  return  prob_total / (double) (cdf_Gauss_n * cdf_Gauss_n * cdf_gamma_n * cdf_gamma_n * cdf_JBeta_n);
}



/*  Use sampling to estimate
 *  ∫ μ,σ  P[D,μ,σ]
 */
double data_prob_1component_bySampling(){
  double curProb, prob_total= 0.0;

  for( uint iter= 0;  iter < sampleRepeatNum; ++iter ){
    Gauss_params params= prior_Gauss_params_sample();
    curProb= 1.0;
    for(  uint d= 0;  d < dataN;  ++d ){
      curProb *= GSLfun_ran_gaussian_pdf( data[d], params );
    }
    prob_total += curProb;
  }
  return  prob_total / (double) sampleRepeatNum;
}



/*  Use sampling to estimate
 *  ∫ m,μ₁,σ₁,μ₂,σ₂  P[D,m,μ₁,σ₁,μ₂,σ₂]
 */
double data_prob_2component_bySampling(){
  double curProb, prob_total= 0.0;

  for( uint iter= 0;  iter < sampleRepeatNum; ++iter ){
    Gauss_mixture_params params=  prior_Gauss_mixture_params_sample();
    curProb= 1.0;
    for( uint i= 0; i < dataN; ++i ){
      double newProb=
        (1-params.mixCof) * GSLfun_ran_gaussian_pdf( data[i], params.Gauss2 )
        +  params.mixCof  * GSLfun_ran_gaussian_pdf( data[i], params.Gauss1 );
      curProb *= newProb;
    }
    prob_total += curProb;
  }
  return  prob_total / (double) sampleRepeatNum;
}



int main( int argc, char *argv[] ){

  uint datasets_n= 10;

  {
    char usage_fmt[]=  "Usage: %s [num_datasets]\n";
    switch( argc ){
    case 1:
      break;
    case 2:
      datasets_n=  atoi( argv[1] );
      if( !datasets_n ){
        printf(  usage_fmt, argv[0]  );
        exit( 64 );
      }
      break;
    default:
      printf(  usage_fmt, argv[0]  );
      exit( 64 );
    }
  }

  GSLfun_setup();
  double prob_data1_bySampling, prob_data2_bySampling;
  double prob_data1_bySumming,  prob_data2_bySumming;

  cdfInv_precompute();


  uint model1_sampling_favors1=  0;
  uint model1_summing__favors1=  0;
  uint model2_sampling_favors1=  0;
  uint model2_summing__favors1=  0;



  printf(  "Starting computation for %d datasets each. ...\n",  datasets_n  );

  printf( "\nData generated with one component\n" );
  for(  uint iter= 0;  iter < datasets_n;  ++iter  ){
    Gauss_params model_params = prior_Gauss_params_sample();
    printf(  "generating data with: (μ,σ) =  (%4.2f,%4.2f)\n", model_params.mu, model_params.sigma  );
    data_generate_1component( model_params );

    prob_data1_bySampling=  data_prob_1component_bySampling();
    prob_data2_bySampling=  data_prob_2component_bySampling();
    prob_data1_bySumming =  data_prob_1component_bySumming();
    prob_data2_bySumming =  data_prob_2component_bySumming();
    printf( "Integrals by sampling= (%g,%g)  by summing: (%g,%g)\n\n",
            prob_data1_bySampling, prob_data2_bySampling,
            prob_data1_bySumming, prob_data2_bySumming );
    if(  prob_data1_bySampling > prob_data2_bySampling  )   ++model1_sampling_favors1;
    if(  prob_data1_bySumming  > prob_data2_bySumming   )   ++model1_summing__favors1;
  }


  printf( "\nData generated with two components\n" );
  for(  uint iter= 0;  iter < datasets_n;  ++iter  ){
    Gauss_mixture_params model_params=  prior_Gauss_mixture_params_sample();
    printf(  "generating data with:  m; (μ1,σ1); (μ2,σ2) =  %5.3f; (%4.2f,%4.2f); (%4.2f,%4.2f)\n",
             model_params.mixCof,
             model_params.Gauss1.mu, model_params.Gauss1.sigma,
             model_params.Gauss2.mu, model_params.Gauss2.sigma  );
    data_generate_2component( model_params );

    prob_data1_bySampling=  data_prob_1component_bySampling();
    prob_data2_bySampling=  data_prob_2component_bySampling();
    prob_data1_bySumming =  data_prob_1component_bySumming();
    prob_data2_bySumming =  data_prob_2component_bySumming();
    printf( "Integrals by sampling= (%g,%g)  by summing: (%g,%g)\n\n",
            prob_data1_bySampling, prob_data2_bySampling,
            prob_data1_bySumming, prob_data2_bySumming );
    if(  prob_data1_bySampling > prob_data2_bySampling  )   ++model2_sampling_favors1;
    if(  prob_data1_bySumming  > prob_data2_bySumming   )   ++model2_summing__favors1;
  }

  printf(  "By sampling: Model1 data, correct selection %u/%u\n", model1_sampling_favors1, datasets_n  );
  printf(  "             Model2 data, correct selection %u/%u\n", (datasets_n - model2_sampling_favors1), datasets_n  );
  printf(  "By summing:  Model1 data, correct selection %u/%u\n", model1_summing__favors1, datasets_n  );
  printf(  "             Model2 data, correct selection %u/%u\n", (datasets_n - model2_summing__favors1), datasets_n  );
}
