/*
    -- MAGMA (version 1.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2010

       @precisions normal z -> s d c

*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cuda_runtime_api.h>
#include <cublas.h>

// includes, project
#include "flops.h"
#include "magma.h"
#include "magma_lapack.h"
#include "magma_zbulge.h"
#include "testings.h"

#if defined(USEMKL)
#include <mkl_service.h>
#endif
#if defined(USEACML)
#include <omp.h>
#endif

// Flops formula
#define PRECISION_z
#if defined(PRECISION_z) || defined(PRECISION_c)
#define FLOPS(n) ( 6. * FMULS_HETRD(n) + 2. * FADDS_HETRD(n))
#else
#define FLOPS(n) (      FMULS_HETRD(n) +      FADDS_HETRD(n))
#endif


extern "C"  magma_int_t magma_zheevdx_2stage(char jobz, char range, char uplo, magma_int_t n,
                                     cuDoubleComplex *a, magma_int_t lda, double vl, double vu, magma_int_t il, magma_int_t iu,
                                     magma_int_t *m, double *w, cuDoubleComplex *work, magma_int_t lwork,
                                     double *rwork, magma_int_t lrwork, magma_int_t *iwork, magma_int_t liwork, magma_int_t *info);

extern "C"  magma_int_t magma_zheevdx_2stage_m(int nrgpu, char jobz, char range, char uplo, magma_int_t n,
                                     cuDoubleComplex *a, magma_int_t lda, double vl, double vu, magma_int_t il, magma_int_t iu,
                                     magma_int_t *m, double *w, cuDoubleComplex *work, magma_int_t lwork,
                                     double *rwork, magma_int_t lrwork, magma_int_t *iwork, magma_int_t liwork, magma_int_t *info);





extern "C" magma_int_t
magma_zhetrd_he2hb( char uplo, magma_int_t n, magma_int_t NB,
                    cuDoubleComplex *a, magma_int_t lda,
                    cuDoubleComplex *tau,
                    cuDoubleComplex *work, magma_int_t lwork,
                    cuDoubleComplex *dT, magma_int_t threads,  
                    magma_int_t *info);

extern "C" magma_int_t
magma_zhetrd_bhe2trc( int THREADS, int WANTZ, char uplo, int NE, int n, int NB, 
                   cuDoubleComplex *A, int LDA, double *D, double *E, cuDoubleComplex *dT1, int ldt1);

extern "C" magma_int_t
magma_zhetrd_bhe2trc_v5(magma_int_t threads, magma_int_t wantz, char uplo, magma_int_t ne, magma_int_t n, magma_int_t nb,
                        cuDoubleComplex *A, magma_int_t lda, double *D, double *E,
                        cuDoubleComplex *dT1, magma_int_t ldt1);

#if defined(PRECISION_z) || defined(PRECISION_d)
extern "C" void cmp_vals(int n, double *wr1, double *wr2, double *nrmI, double *nrm1, double *nrm2);
extern "C" void zcheck_eig_(char *JOBZ, int  *MATYPE, int  *N, int  *NB,
                       cuDoubleComplex* A, int  *LDA, double *AD, double *AE, double *D1, double *EIG,
                    cuDoubleComplex *Z, int  *LDZ, cuDoubleComplex *WORK, double *RWORK, double *RESU);
#endif

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing zhetrd_he2hb
*/
int main( int argc, char** argv)
{
    TESTING_CUDA_INIT_MGPU();

    magma_timestr_t       start, end, tband;
    double           eps, flops, gpu_perf, gpu_time;
    cuDoubleComplex *h_A, *h_R, *h_work, *dT1;
    cuDoubleComplex *tau;
    double *D, *E;

    /* Matrix size */
    magma_int_t N = 0, n2, lda, lwork,ldt,lwork0;
    magma_int_t size[10] = {1024,2048,3072,4032,5184,6016,7040,8064,9088,10112};

    magma_int_t i, j, k, info, checkres, once = 0;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};
    const char *uplo = MagmaLowerStr;

    magma_int_t WANTZ=0;
    magma_int_t THREADS=1;
    magma_int_t NE = 0;
    magma_int_t NB = 0;
    magma_int_t ngpu = 1;

    checkres  = 0; //getenv("MAGMA_TESTINGS_CHECK") != NULL;

    if (argc != 1){
        for(i = 1; i<argc; i++){
            if (strcmp("-N", argv[i])==0) {
                N = atoi(argv[++i]);
                once = 1;
            }
            else if (strcmp("-NB", argv[i])==0) {
                NB = atoi(argv[++i]);
            }
            else if (strcmp("-threads", argv[i])==0) {
                THREADS = atoi(argv[++i]);
            }
            else if (strcmp("-ngpu", argv[i])==0) {
                ngpu = atoi(argv[++i]);
            }
            else if (strcmp("-wantz", argv[i])==0) {
                WANTZ = atoi(argv[++i]);
            }
            else if (strcmp("-NE", argv[i])==0) {
                NE = atoi(argv[++i]);
            }
            else if ( strcmp("-c", argv[i]) == 0 ) {
                checkres = 1;
            }
            else if (strcmp("-U", argv[i])==0)
                uplo = MagmaUpperStr;
            else if (strcmp("-L", argv[i])==0)
                uplo = MagmaLowerStr;
        }
        if ( N > 0 )
            printf("  testing_zhetrd_he2hb -L|U -N %d -NB %d   -wantz %d   -threads %d    check %d \n\n", N, NB, WANTZ, THREADS, checkres);
        else
        {
            printf("\nUsage: \n");
            printf("  testing_zhetrd_he2hb -L|U -N %d -NB  -wantz -threads \n\n", 1024);
            exit(1);
        }
    }
    else {
        printf("\nUsage: \n");
        printf("  testing_zhetrd_he2hb -L|U -N %d\n\n", 1024);
        N = size[9];
    }
        
 printf ("HELLOOOOOOOO\n");

    eps = lapackf77_dlamch( "E" );
    lda = N;
    ldt = N;
    n2  = lda * N; 
    if(NB<1)
        NB  = 64; //64; //magma_get_zhetrd_he2hb_nb(N);

    if(NE<1)
        NE  = N; //64; //magma_get_zhetrd_he2hb_nb(N);

    /* We suppose the magma NB is bigger than lapack NB */
    lwork0 = N*NB; 

    /* Allocate host memory for the matrix */
    TESTING_MALLOC(    h_A,    cuDoubleComplex, lda*N );
    TESTING_HOSTALLOC( h_R,    cuDoubleComplex, lda*N );
    TESTING_HOSTALLOC( h_work, cuDoubleComplex, lwork0 );
    TESTING_MALLOC(    tau,    cuDoubleComplex, N-1   );
    TESTING_HOSTALLOC( D,    double, N );
    TESTING_HOSTALLOC( E,    double, N );
    //TESTING_DEVALLOC( dT1,  cuDoubleComplex, (2*min(N,N)+(N+31)/32*32)*NB );
    TESTING_DEVALLOC( dT1,  cuDoubleComplex, (N*NB) );

    printf("  N    GPU GFlop/s   \n");
    printf("=====================\n");
    for(i=0; i<10; i++){
        if ( !once ) {
            N = size[i];
        }
        lda  = N;
        n2   = N*lda;
        flops = FLOPS_ZHETRD( (double)N ) / 1e6;
        
       // if(WANTZ) flops = 2.0*flops;

        /* ====================================================================
           Initialize the matrix
           =================================================================== */
        lapackf77_zlarnv( &ione, ISEED, &n2, h_A );

        // Make the matrix hermitian 
        {
            magma_int_t i, j;
            for(i=0; i<N; i++) {
                MAGMA_Z_SET2REAL( h_A[i*lda+i], ( MAGMA_Z_REAL(h_A[i*lda+i]) ) );
                for(j=0; j<i; j++)
                    h_A[i*lda+j] = cuConj(h_A[j*lda+i]);
            }
        }


        lapackf77_zlacpy( MagmaUpperLowerStr, &N, &N, h_A, &lda, h_R, &lda );


        /* ====================================================================
           Performs operation using MAGMA
           =================================================================== */
        char *jobz = (char*)MagmaVectorsStr;
        char range = 'A';
        magma_int_t fraction_ev = 100;
        magma_int_t il,iu,m1,m2;
        double vl,vu;

        if (fraction_ev == 0){
            il = N / 10;
            iu = N / 5+il;
        }
        else {
            il = 1;
            iu = (int)(fraction_ev*N);
            if (iu < 1) iu = 1;
        }

        cuDoubleComplex *hh_work;
        double *rwork;
        magma_int_t *iwork;
       

    
        magma_device_t cdev;
        magma_getdevice( &cdev );
   



        start = get_current_time();
/*        
        magma_zhetrd_he2hb(uplo[0], N, NB, h_R, lda, tau, h_work, lwork0, dT1, THREADS, &info);
        tband = get_current_time();
        printf("  Finish BAND  N %d  NB %d  ngpu %d timing= %lf \n" ,N,NB,ngpu,GetTimerValue(start,tband) / 1000.);
        magma_zhetrd_bhe2trc_v5(THREADS, WANTZ, uplo[0], NE, N, NB, h_R, lda, D, E, dT1, ldt);
*/

/*
        magma_zhetrd_he2hb(uplo[0], N, NB, h_R, lda, tau, h_work, lwork, dT1, THREADS, &info);
        tband = get_current_time();
        printf("  Finish BAND  N %d  NB %d  ngpu %d timing= %lf \n" ,N,NB,ngpu,GetTimerValue(start,tband) / 1000.);
        magma_zhetrd_bhe2trc(THREADS, WANTZ, uplo[0], NE, N, NB, h_R, lda, D, E, dT1, ldt);
*/

        magma_int_t nb = magma_get_zhetrd_nb(N);
        magma_int_t lwork = magma_zbulge_get_lq2(N) + 2*N + N*N;
        magma_int_t lrwork = 1 + 5*N +2*N*N;
        magma_int_t liwork = 3 + 5*N;
       
        TESTING_HOSTALLOC(hh_work, cuDoubleComplex,  lwork);
        TESTING_HOSTALLOC( rwork,          double, lrwork);
        TESTING_MALLOC(    iwork,     magma_int_t, liwork);

        if(ngpu==1){
            printf("calling zheevdx_2stage 1 GPU\n");
            magma_zheevdx_2stage(jobz[0], range, uplo[0], N, h_R, lda, vl, vu, il, iu, &m1, D, hh_work, lwork, rwork, lrwork, iwork, liwork, &info);
        }else{
            printf("calling zheevdx_2stage_m %d GPU\n",ngpu);
            return 0;
            //magma_zheevdx_2stage_m(ngpu,jobz[0], range, uplo[0], N, h_R, lda, vl, vu, il, iu, &m1, D, hh_work, lwork, rwork, lrwork, iwork, liwork, &info);
        }


       cudaSetDevice( cdev );
        
       end = get_current_time();




        gpu_perf = flops / GetTimerValue(start,end);
        gpu_time = GetTimerValue(start,end) / 1000.;

        /* =====================================================================
           Check the factorization
           =================================================================== */
        /*
        if ( checkres ) {
            FILE        *fp ;

            printf("Writing input matrix in matlab_i_mat.txt ...\n");
            fp = fopen ("matlab_i_mat.txt", "w") ;
            if( fp == NULL ){ printf("Couldn't open output file\n"); exit(1);}

            for(j=0; j<N; j++)
                for(k=0; k<N; k++)
                    {
                        #if defined(PRECISION_z) || defined(PRECISION_c)
                        fprintf(fp, "%5d %5d %11.8f %11.8f\n", k+1, j+1, 
                                h_A[k+j*lda].x, h_A[k+j*lda].y);
                        #else
                        fprintf(fp, "%5d %5d %11.8f\n", k+1, j+1, h_A[k+j*lda]);
                        #endif
                    }
            fclose( fp ) ;

          printf("Writing output matrix in matlab_o_mat.txt ...\n");
          fp = fopen ("matlab_o_mat.txt", "w") ;
          if( fp == NULL ){ printf("Couldn't open output file\n"); exit(1);}

          for(j=0; j<N; j++)
            for(k=0; k<N; k++)
              {
                #if defined(PRECISION_z) || defined(PRECISION_c)
                fprintf(fp, "%5d %5d %11.8f %11.8f\n", k+1, j+1,
                        h_R[k+j*lda].x, h_R[k+j*lda].y);
                #else
                fprintf(fp, "%5d %5d %11.8f\n", k+1, j+1, h_R[k+j*lda]);
                #endif
              } 
          fclose( fp ) ;

        }*/



        /* =====================================================================
           Print performance and error.
           =================================================================== */
#if defined(CHECKEIG)
#if defined(PRECISION_z)  || defined(PRECISION_d)
        if ( checkres ) {
            printf("  Total N %5d  flops %6.2f  timing %6.2f seconds\n", (int) N, gpu_perf, gpu_time );
            char JOBZ;
            if(WANTZ==0) 
                    JOBZ='N';
            else
                    JOBZ = 'V';
            double nrmI=0.0, nrm1=0.0, nrm2=0.0;
            int    lwork2 = 256*N;
            cuDoubleComplex *work2     = (cuDoubleComplex *) malloc (lwork2*sizeof(cuDoubleComplex));
            double *rwork2     = (double *) malloc (N*sizeof(double));
            double *D2          = (double *) malloc (N*sizeof(double));
            cuDoubleComplex *AINIT    = (cuDoubleComplex *) malloc (N*lda*sizeof(cuDoubleComplex));
            memcpy(AINIT, h_A, N*lda*sizeof(cuDoubleComplex));
            /* compute the eigenvalues using lapack routine to be able to compare to it and used as ref */
            start = get_current_time();
            i= min(12,THREADS);

#if defined(USEMKL)
            mkl_set_num_threads( i );
#endif
#if defined(USEACML)
            omp_set_num_threads(i);
#endif

#if defined(PRECISION_z) || defined (PRECISION_c)
            lapackf77_zheev( "N", "L", &N, h_A, &lda, D2, work2, &lwork2, rwork2, &info );
#else
            lapackf77_dsyev( "N", "L", &N, h_A, &lda, D2, work2, &lwork2, &info );
#endif
            ///* call eigensolver for our resulting tridiag [D E] and for Q */
            //dstedc_withZ('V', N, D, E, h_R, lda);
            ////dsterf_( &N, D, E, &info); 
            ////
            end = get_current_time();
            printf("  Finish CHECK - EIGEN   timing= %lf  threads %d \n" ,GetTimerValue(start,end) / 1000., i);

            /*
            for(i=0;i<10;i++)
                printf(" voici lpk D[%d] %e\n",i,D2[i]);
            */

            //cuDoubleComplex mydz=0.0,mydo=1.0;
            //cuDoubleComplex *Z = (cuDoubleComplex *) malloc(N*lda*sizeof(cuDoubleComplex));
           // dgemm_("N","N",&N,&N,&N,&mydo,h_R,&lda,h_A,&lda,&mydz,Z,&lda);


           /* compare result */
           cmp_vals(N, D2, D, &nrmI, &nrm1, &nrm2);


           cuDoubleComplex *WORKAJETER;
           double *RWORKAJETER, *RESU;
           WORKAJETER  = (cuDoubleComplex *) malloc( (2* N * N + N) * sizeof(cuDoubleComplex) );
           RWORKAJETER = (double *) malloc( N * sizeof(double) );
           RESU        = (double *) malloc(10*sizeof(double));
           int MATYPE;
           memset(RESU,0,10*sizeof(double));

 
           MATYPE=3;
           double NOTHING=0.0;
           start = get_current_time();
           // check results
           zcheck_eig_(&JOBZ, &MATYPE, &N, &NB, AINIT, &lda, &NOTHING, &NOTHING, D2 , D, h_R, &lda, WORKAJETER, RWORKAJETER, RESU );
           end = get_current_time();
           printf("  Finish CHECK - results timing= %lf \n" ,GetTimerValue(start,end) / 1000.);
#if defined(USEMKL)
           mkl_set_num_threads( 1 );
#endif
#if defined(USEACML)
           omp_set_num_threads(1);
#endif

           printf("\n");
           printf(" ================================================================================================================\n");
           printf("   ==> INFO voici  threads=%d    N=%d    NB=%d   WANTZ=%d\n", (int) THREADS, (int) N, (int) NB, (int) WANTZ);
           printf(" ================================================================================================================\n");
           printf("            DSBTRD                : %15s \n", "STATblgv9withQ    ");
           printf(" ================================================================================================================\n");
           if(WANTZ>0)
              printf(" | A - U S U' | / ( |A| n ulp )   : %15.3E   \n",RESU[0]); 
           if(WANTZ>0)
              printf(" | I - U U' | / ( n ulp )         : %15.3E   \n", RESU[1]);
           printf(" | D1 - EVEIGS | / (|D| ulp)      : %15.3E   \n",  RESU[2]);
           printf(" max | D1 - EVEIGS |              : %15.3E   \n",  RESU[6]);
           printf(" ================================================================================================================\n\n\n");
       
           printf(" ****************************************************************************************************************\n");
           printf(" * Hello here are the norm  Infinite (max)=%e  norm one (sum)=%e   norm2(sqrt)=%e *\n",nrmI, nrm1, nrm2);
           printf(" ****************************************************************************************************************\n\n");

        } 
#endif         
#endif  

      printf("  Total N %5d  flops %6.2f        timing %6.2f seconds\n", (int) N, gpu_perf, gpu_time );
      printf("============================================================================\n\n\n");

      if ( once )
          break;
    }

    /* Memory clean up */
    TESTING_FREE( h_A );
    TESTING_FREE( tau ); 
    TESTING_HOSTFREE( h_R ); 
    TESTING_HOSTFREE( h_work ); 

    /* Shutdown */
    TESTING_CUDA_FINALIZE_MGPU();
    return EXIT_SUCCESS;
}
