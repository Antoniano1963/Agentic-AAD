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

  /* v023: intensify the best tour of this search (monotone, bounded, in-budget) */
  {
    int gained = this->PolishOrOpt( tBest );
    if( gained > 0 )
      eax_curve.observe( tBest.fEvaluationValue, fCurNumOfGen, fStage, "polish" );
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
  if ( fAverageValue - fBestValue < 0.001 )  
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
      return true;
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




/* v023: Or-opt intensification of one tour.  Segments of 1..3 consecutive cities
   are moved next to neighbour-list cities of their end points; every candidate
   move is validated by recomputing the full tour length and is reverted unless it
   shortens the tour, so the tour can only improve. */
/* ---- v026: helpers for the post-search intensification ---- */

static int EaxTourLen( TEvaluator* ev, int N, const int* tour )
{
  int len = 0;
  for( int i = 0; i < N; ++i ) len += ev->Direct( tour[ i ], tour[ ( i + 1 ) % N ] );
  return len;
}

static void EaxDoubleBridge( int* tour, int N, int* scratch )
{
  if( N < 8 ) return;
  int p1 = rand() % N, p2 = rand() % N, p3 = rand() % N;
  if( p1 > p2 ){ int t = p1; p1 = p2; p2 = t; }
  if( p2 > p3 ){ int t = p2; p2 = p3; p3 = t; }
  if( p1 > p2 ){ int t = p1; p1 = p2; p2 = t; }
  if( p1 == p2 || p2 == p3 ) return;
  int k = 0;
  for( int i = 0; i <= p1; ++i ) scratch[ k++ ] = tour[ i ];
  for( int i = p2 + 1; i <= p3; ++i ) scratch[ k++ ] = tour[ i ];
  for( int i = p1 + 1; i <= p2; ++i ) scratch[ k++ ] = tour[ i ];
  for( int i = p3 + 1; i < N; ++i ) scratch[ k++ ] = tour[ i ];
  for( int i = 0; i < N; ++i ) tour[ i ] = scratch[ i ];
}

/* Or-opt descent: move segments of 1..3 cities next to neighbour-list cities of
   their end points; monotone (every move validated by a full-length recompute). */
static void EaxOrOptDescent( TEvaluator* ev, int N, int* tour, int* posi,
                             int* nt, int* cand, int& len, int maxPasses )
{
  bool moved = true;
  int passes = 0;
  while( moved && passes++ < maxPasses ){
    moved = false;
    for( int L = 1; L <= 3 && !moved; ++L ){
      for( int i = 0; i < N && !moved; ++i ){
        int iEnd = i + L - 1;
        if( iEnd >= N ) continue;
        int s0 = tour[ i ], s1 = tour[ iEnd ];
        int p  = tour[ ( i - 1 + N ) % N ];
        int q  = tour[ ( iEnd + 1 ) % N ];
        int removed = ev->Direct( p, s0 ) + ev->Direct( s1, q ) - ev->Direct( p, q );
        if( removed <= 0 ) continue;

        int bestGain = 0, bestC = -1, bestRev = 0;
        for( int side = 0; side < 2; ++side ){
          int anchor = ( side == 0 ) ? s0 : s1;
          for( int k = 1; k <= ev->fNearNumMax; ++k ){
            int c  = ev->fNearCity[ anchor ][ k ];
            int cp = posi[ c ];
            if( cp >= i && cp <= iEnd ) continue;
            int cn  = tour[ ( cp + 1 ) % N ];
            int cnp = posi[ cn ];
            if( cnp >= i && cnp <= iEnd ) continue;
            int gainF = removed - ( ev->Direct( c, s0 ) + ev->Direct( s1, cn ) - ev->Direct( c, cn ) );
            if( gainF > bestGain ){ bestGain = gainF; bestC = c; bestRev = 0; }
            int gainR = removed - ( ev->Direct( c, s1 ) + ev->Direct( s0, cn ) - ev->Direct( c, cn ) );
            if( gainR > bestGain ){ bestGain = gainR; bestC = c; bestRev = 1; }
          }
        }
        if( bestC < 0 || bestGain <= 0 ) continue;

        int seg[ 3 ];
        for( int t = 0; t < L; ++t ) seg[ t ] = tour[ i + t ];

        int k2 = 0;
        for( int t = 0; t < N; ++t )
          if( t < i || t > iEnd ) nt[ k2++ ] = tour[ t ];

        int ip = -1;
        for( int t = 0; t < k2; ++t )
          if( nt[ t ] == bestC ){ ip = t; break; }
        if( ip < 0 ) continue;

        int k3 = 0;
        for( int t = 0; t <= ip; ++t ) cand[ k3++ ] = nt[ t ];
        if( bestRev == 0 ){ for( int t = 0; t < L; ++t ) cand[ k3++ ] = seg[ t ]; }
        else              { for( int t = L - 1; t >= 0; --t ) cand[ k3++ ] = seg[ t ]; }
        for( int t = ip + 1; t < k2; ++t ) cand[ k3++ ] = nt[ t ];
        if( k3 != N ) continue;

        int nlen = EaxTourLen( ev, N, cand );
        if( nlen < len ){
          for( int t = 0; t < N; ++t ) tour[ t ] = cand[ t ];
          for( int t = 0; t < N; ++t ) posi[ tour[ t ] ] = t;
          len = nlen;
          moved = true;
        }
      }
    }
  }
}

int TEnvironment::PolishOrOpt( TIndi& indi )
{
  int N = fEvaluator->Ncity;
  if( N < 10 ) return 0;

  int* tour  = new int [ N ];
  int* posi  = new int [ N ];
  int* nt    = new int [ N ];
  int* cand  = new int [ N ];
  int* bestT = new int [ N ];

  int curr = 0, pre = -1, st = 0, cnt = 0, next = 0;
  while( 1 ){
    tour[ cnt++ ] = curr;
    if( indi.fLink[ curr ][ 0 ] == pre ) next = indi.fLink[ curr ][ 1 ];
    else                                 next = indi.fLink[ curr ][ 0 ];
    pre = curr; curr = next;
    if( curr == st ) break;
    if( cnt >= N ) break;
  }
  if( cnt != N ){
    delete [] tour; delete [] posi; delete [] nt; delete [] cand; delete [] bestT;
    return 0;
  }
  for( int i = 0; i < N; ++i ) posi[ tour[ i ] ] = i;

  int before = indi.fEvaluationValue;
  int len = EaxTourLen( fEvaluator, N, tour );

  /* phase 1: descent from the reported tour */
  EaxOrOptDescent( fEvaluator, N, tour, posi, nt, cand, len, 12 );
  int bestLen = len;
  for( int i = 0; i < N; ++i ) bestT[ i ] = tour[ i ];

  /* phase 2: bounded iterated local search */
  clock_t c0 = clock();
  int iters = 0;
  while( iters++ < 25 ){
    if( (double)( clock() - c0 ) / (double)CLOCKS_PER_SEC > 1.5 ) break;
    EaxDoubleBridge( tour, N, cand );
    len = EaxTourLen( fEvaluator, N, tour );
    EaxOrOptDescent( fEvaluator, N, tour, posi, nt, cand, len, 12 );
    if( len < bestLen ){
      bestLen = len;
      for( int i = 0; i < N; ++i ) bestT[ i ] = tour[ i ];
    }
    else{
      len = bestLen;
      for( int i = 0; i < N; ++i ) tour[ i ] = bestT[ i ];
      for( int i = 0; i < N; ++i ) posi[ tour[ i ] ] = i;
    }
  }

  /* write back only if the (recomputed) length really improved */
  if( bestLen < before ){
    for( int i = 0; i < N; ++i ){
      indi.fLink[ bestT[ i ] ][ 0 ] = bestT[ ( i - 1 + N ) % N ];
      indi.fLink[ bestT[ i ] ][ 1 ] = bestT[ ( i + 1 ) % N ];
    }
    fEvaluator->DoIt( indi );
    fEvaluator->TranceLinkOrder( indi );
  }

  delete [] tour; delete [] posi; delete [] nt; delete [] cand; delete [] bestT;
  if( indi.fEvaluationValue < before ) return before - indi.fEvaluationValue;
  return 0;
}

