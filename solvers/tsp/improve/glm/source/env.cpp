#include "convergence.h"
#ifndef __ENVIRONMENT__
#include "env.h"
#endif

#include <math.h> 
     
void MakeRandSol( TEvaluator* eval , TIndi& indi );
void Make2optSol( TEvaluator* eval , TIndi& indi );

TEnvironment::TEnvironment()
{
  fEvaluator = new TEvaluator();
  fFinalSearch = false;
}


TEnvironment::~TEnvironment()
{
  delete [] fIndexForMating;
  delete [] tCurPop;
  delete fEvaluator;
  delete tCross;

  int N = fEvaluator->Ncity;
  for( int i = 0; i < N; ++i ) 
    delete [] fEdgeFreq[ i ];
  delete [] fEdgeFreq;
}


void TEnvironment::Define()
{
  fEvaluator->SetInstance( fFileNameTSP );
  int N = fEvaluator->Ncity;

  fIndexForMating = new int [ fNumOfPop + 1 ];  

  tCurPop = new TIndi [ fNumOfPop ];
  for ( int i = 0; i < fNumOfPop; ++i )
    tCurPop[i].Define( N );

  tBest.Define( N );

  tCross = new TCross( N );
  tCross->eval = fEvaluator;                 
  tCross->fNumOfPop = fNumOfPop;             

  tKopt = new TKopt( N );
  tKopt->eval = fEvaluator;
  tKopt->SetInvNearList();

  fEdgeFreq = new int* [ N ]; 
  for( int i = 0; i < N; ++i ) 
    fEdgeFreq[ i ] = new int [ fEvaluator->fNearNumMax+1 ]; // Large
}


void TEnvironment::DoIt()
{
  this->fTimeStart = clock();   

  if( fFileNameInitPop == NULL )
    this->InitPop();                       
  else
    this->ReadPop( fFileNameInitPop );     

  this->fTimeInit = clock();    

  this->Init();
  this->GetEdgeFreq();

  bool hit_time_limit = false;
  while( 1 )
 {
    this->SetAverageBest();
    printf( "%d: %d %lf\n", fCurNumOfGen, fBestValue, fAverageValue );

    eax_curve.observe(tBest.fEvaluationValue, fCurNumOfGen, fStage, "poll");
    eax_curve.record(fCurNumOfGen, fStage, "generation_end", true, fBestValue, fAverageValue);
    if(eax_curve.expired()) { hit_time_limit = true; break; }
    if( this->TerminationCondition() ) break;

    this->SelectForMating();

    for( int s =0; s < fNumOfPop; ++s )
    {
      if(eax_curve.expired()) { hit_time_limit = true; break; }
      this->GenerateKids( s );     
      this->SelectForSurvival( s );
      eax_curve.observe(tCurPop[fIndexForMating[s]].fEvaluationValue, fCurNumOfGen, fStage, "poll"); 
    }
    if(hit_time_limit) {
      this->SetAverageBest();
      break;
    }
    ++fCurNumOfGen;
  }

  eax_curve.observe(tBest.fEvaluationValue, fCurNumOfGen, fStage, "poll");
  eax_curve.finish(fCurNumOfGen, fStage, hit_time_limit ? "time_limit" : "converged");
  this->fTimeEnd = clock();   
}
 
/* See Section 2.2 */
void TEnvironment::Init()
{
  fAccumurateNumCh = 0;
  fCurNumOfGen = 0;
  fStagBest = 0;
  fMaxStagBest = 0;
  fStage = 1;          /* Stage I */
  fFlagC[ 0 ] = 4;     /* Diversity preservation: 1:Greedy, 2:--- , 3:Distance, 4:Entropy (see Section 4) */
  fFlagC[ 1 ] = 1;     /* Eset Type: 1:Single-AB, 2:Block2 (see Section 3) */ 
} 

/* See Section 2.2 */
bool TEnvironment::TerminationCondition()
{
  if ( fAverageValue - fBestValue < 0.001 && !fFinalSearch )  
    return true;

  if( fStage == 1 ) /* Stage I */      
  {
    if( fStagBest == int(1500/fNumOfKids) && fMaxStagBest == 0 ){ /* 1500/N_ch (See Section 2.2) */
      fMaxStagBest =int( fCurNumOfGen / 10 );                  /* fMaxStagBest = G/10 (See Section 2.2) */
    } 
    else if( fMaxStagBest != 0 && fMaxStagBest <= fStagBest ){ /* Terminate Stage I (proceed to Stage II) */
      fStagBest = 0;
      fMaxStagBest = 0;
      fCurNumOfGen1 = fCurNumOfGen;
      fFlagC[ 1 ] = 2; 
      fStage = 2;      
    }
    return false;
  }

  if( fStage == 2 ){ /* Stage II */
    if( fStagBest == int(1500/fNumOfKids) && fMaxStagBest == 0 ){ /* 1500/N_ch (See Section 2.2) */
      fMaxStagBest = int( (fCurNumOfGen - fCurNumOfGen1) / 10 ); /* fMaxStagBest = G/10 (See Section 2.2) */
    } 
    else if( fMaxStagBest != 0 && fMaxStagBest <= fStagBest ){ /* Terminate Stage II and GA */
      return !fFinalSearch;
    }

    return false;
  }
}


void TEnvironment::SetAverageBest() 
{
  int stockBest = tBest.fEvaluationValue;
  
  fAverageValue = 0.0;
  fBestIndex = 0;
  fBestValue = tCurPop[0].fEvaluationValue;
  
  for(int i = 0; i < fNumOfPop; ++i ){
    fAverageValue += tCurPop[i].fEvaluationValue;
    if( tCurPop[i].fEvaluationValue < fBestValue ){
      fBestIndex = i;
      fBestValue = tCurPop[i].fEvaluationValue;
    }
  }
  
  tBest = tCurPop[ fBestIndex ];
  fAverageValue /= (double)fNumOfPop;

  if( tBest.fEvaluationValue < stockBest ){
    fStagBest = 0;
    fBestNumOfGen = fCurNumOfGen;
    fBestAccumeratedNumCh = fAccumurateNumCh;
  }
  else ++fStagBest;
}


void TEnvironment::InitPop()
{
  TIndi partialBest;
  if(eax_curve.total_mode) partialBest.Define(fEvaluator->Ncity);
  int made = 0;
  for ( int i = 0; i < fNumOfPop; ++i ){ 
    tKopt->MakeRandSol( tCurPop[ i ] );
    if(eax_curve.total_mode && (i==0 || tCurPop[i].fEvaluationValue<partialBest.fEvaluationValue)) partialBest=tCurPop[i];
    eax_curve.observe(tCurPop[i].fEvaluationValue, 0, 0, "initialization");    /* Make a random tour */
    if(eax_curve.total_mode && eax_curve.expired()) { tBest=partialBest; throw EaxTotalDeadline(); }
    tKopt->DoIt( tCurPop[ i ] );           /* Apply the local search with the 2-opt neighborhood */ 
    eax_curve.observe(tCurPop[i].fEvaluationValue, 0, 0, "initialization");
    fEvaluator->TranceLinkOrder( tCurPop[ i ] );  // Large
    if(eax_curve.total_mode) {
      if(tCurPop[i].fEvaluationValue<partialBest.fEvaluationValue) partialBest=tCurPop[i];
      if(eax_curve.expired()) { tBest=partialBest; throw EaxTotalDeadline(); }
    }
    ++made;
    if(eax_curve.total_mode && made >= 40 && eax_curve.elapsed() >= 0.10 * eax_curve.limit) break;
  }
  if(eax_curve.total_mode && made < fNumOfPop) {
    fNumOfPop = made;
    tCross->fNumOfPop = made;
    printf("InitPop: population shrunk to %d (init cost cap)" "\n", fNumOfPop);
    fflush(stdout);
  }
}


void TEnvironment::SelectForMating()
{
  /* fIndexForMating[] <-- a random permutation of 0, ..., fNumOfPop-1 */
  tRand->Permutation( fIndexForMating, fNumOfPop, fNumOfPop ); 
  fIndexForMating[ fNumOfPop ] = fIndexForMating[ 0 ];
}

void TEnvironment::SelectForSurvival( int s )
{
}


void TEnvironment::GenerateKids( int s )
{
  tCross->SetParents( tCurPop[fIndexForMating[s]], tCurPop[fIndexForMating[s+1]], fFlagC, fNumOfKids );  
  
  /* Note: tCurPop[fIndexForMating[s]] is replaced with a best offspring solutions in tCorss->DoIt(). 
     fEegeFreq[][] is also updated there. */
  tCross->DoIt( tCurPop[fIndexForMating[s]], tCurPop[fIndexForMating[s+1]], fNumOfKids, 1, fFlagC, fEdgeFreq );

  fAccumurateNumCh += tCross->fNumOfGeneratedCh;
}


void TEnvironment::GetEdgeFreq()  // Large
{
  int N = fEvaluator->Ncity;
  int k0, k1;
  
  for( int j1 = 0; j1 < N; ++j1 )
    for( int j2 = 0; j2 < fEvaluator->fNearNumMax; ++j2 ) 
      fEdgeFreq[ j1 ][ j2 ] = 0;

  
  for( int i = 0; i < fNumOfPop; ++i )
  {
    for(int j = 0; j < N; ++j )
    {
      k0 = tCurPop[ i ].fOrder[ j ][ 0 ];      
      k1 = tCurPop[ i ].fOrder[ j ][ 1 ];
      if( k0 != -1 ) 
	++fEdgeFreq[ j ][ k0 ];
      if( k1 != -1 ) 
      ++fEdgeFreq[ j ][ k1 ];
    }
  }
}


void TEnvironment::PrintOn( int n, char* dstFile ) 
{
  printf( "n = %d val = %d Gen = %d Time = %d %d\n" , 
	  n, 
	  tBest.fEvaluationValue, 
	  fCurNumOfGen, 
	  (int)((double)(this->fTimeInit - this->fTimeStart)/(double)CLOCKS_PER_SEC), 
	  (int)((double)(this->fTimeEnd - this->fTimeStart)/(double)CLOCKS_PER_SEC) );
  fflush(stdout);

  FILE *fp;
  char filename[ 80 ];
  sprintf( filename, "%s_Result", dstFile );
  fp = fopen( filename, "a");
  
  fprintf( fp, "%d %d %d %d %d\n" , 
	   n, 
	   tBest.fEvaluationValue, 
	   fCurNumOfGen, 
	   (int)((double)(this->fTimeInit - this->fTimeStart)/(double)CLOCKS_PER_SEC), 
	   (int)((double)(this->fTimeEnd - this->fTimeStart)/(double)CLOCKS_PER_SEC) );
  
  fclose( fp );
}


void TEnvironment::WriteBest( char* dstFile ) 
{
  FILE *fp;
  char filename[ 80 ];
  sprintf( filename, "%s_BestSol", dstFile );
  fp = fopen( filename, "a");
  
  fEvaluator->WriteTo( fp, tBest );

  fclose( fp );
}


void TEnvironment::WritePop( int n, char* dstFile ) 
{
  FILE *fp;
  char filename[ 80 ];
  sprintf( filename, "%s_POP_%d", dstFile, n );
  fp = fopen( filename, "w");

  for( int s = 0; s < fNumOfPop; ++s )
    fEvaluator->WriteTo( fp, tCurPop[ s ] );

  fclose( fp );
}


void TEnvironment::ReadPop( char* fileName )
{
  FILE* fp;

  if( ( fp = fopen( fileName, "r" ) ) == NULL ){
    printf( "Read Error1\n"); 
    fflush( stdout );
    exit( 1 );
  }

  for ( int i = 0; i < fNumOfPop; ++i ){ 
    if( fEvaluator->ReadFrom( fp, tCurPop[ i ] ) == false ){
      printf( "Read Error2\n"); 
      fflush( stdout );
      exit( 1 );
    }
    fEvaluator->TranceLinkOrder( tCurPop[ i ] );  // Large
  }
  fclose( fp );
}


