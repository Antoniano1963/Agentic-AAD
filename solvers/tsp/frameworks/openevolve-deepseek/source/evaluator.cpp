#ifndef __EVALUATOR__
#include "evaluator.h"
#endif

#include <algorithm>
#include <vector>
#include <utility>

TEvaluator::TEvaluator()
{
  fEdgeDisOrder = NULL;
  fNearCity = NULL;
  x = NULL;
  y = NULL;
  Ncity = 0;
  fNearNumMax = 100;                               // Large
}

TEvaluator::~TEvaluator()
{
  if( fEdgeDisOrder != NULL ) {
    for ( int i = 0; i < Ncity; ++i ) 
      delete [] fEdgeDisOrder[ i ];
    delete [] fEdgeDisOrder;
  }
  if( fNearCity != NULL ) {
    for ( int i = 0; i < Ncity; ++i ) 
      delete [] fNearCity[ i ];
    delete [] fNearCity;
  }
  
  delete [] x;
  delete [] y;
}

void TEvaluator::SetInstance( char filename[] )
{
  FILE* fp;
  int flag;
  int n;
  char word[ 80 ];
  int *DisTmp;

  fp = fopen( filename, "r" );


  ////// read instance //////
  while( 1 ){
    if( fscanf( fp, "%s", word ) == EOF )
      break;

    if( strcmp( word, "DIMENSION" ) == 0 ){
      fscanf( fp, "%s", word ); 
      assert( strcmp( word, ":" ) == 0 );
      fscanf( fp, "%d", &Ncity ); 
    } 

    if( strcmp( word, "EDGE_WEIGHT_TYPE" ) == 0 ){
      fscanf( fp, "%s", word ); 
      assert( strcmp( word, ":" ) == 0 );
      fscanf( fp, "%s", fType ); 
    } 

    if( strcmp( word, "NODE_COORD_SECTION" ) == 0 ) 
      break;
      

  }
  if( strcmp( word, "NODE_COORD_SECTION" ) != 0 ){
    printf( "Error in reading the instance\n" );
    exit(0);
  }

  x = new double [ Ncity ]; 
  y = new double [ Ncity ]; 

  int xi, yi;
  for( int i = 0; i < Ncity; ++i ) 
  {
    fscanf( fp, "%d", &n );
    assert( i+1 == n ); 
    fscanf( fp, "%s", word ); 
    x[ i ] = atof( word );
    fscanf( fp, "%s", word ); 
    y[ i ] = atof( word );
  }

  fclose(fp);
  //////////////////////////

  fEdgeDisOrder = new int* [ Ncity ];         // Large
  for( int i = 0; i < Ncity; ++i ) 
    fEdgeDisOrder[ i ] = new int [ fNearNumMax+1 ];
  fNearCity = new int* [ Ncity ];
  for( int i = 0; i < Ncity; ++i ) 
    fNearCity[ i ] = new int [ fNearNumMax+1 ];

  if( strcmp( fType, "EUC_2D" ) != 0 ){        
    printf( "Please modify program code in TEvaluator::Direct( ) appropriately when EDGE_WEIGHT_TYPE is not EUC_2D.\n" );
    exit( 1 );  /* After the modification, remove this line. */
  }

  // Efficiently build the fNearNumMax nearest-neighbour lists.
  // The old implementation repeatedly scanned all cities to extract each
  // neighbour, taking O(Ncity^2 * fNearNumMax).  A partial sort per city
  // reduces this to O(Ncity^2 + Ncity * fNearNumMax log fNearNumMax).
  for( int i = 0; i < Ncity ; ++i )
  {
    std::vector< std::pair<double,int> > cand;
    cand.reserve( Ncity > 0 ? Ncity-1 : 0 );
    for( int j = 0; j < Ncity ; ++j )
    {
      if( j == i ) continue;
      double dx = x[i] - x[j];
      double dy = y[i] - y[j];
      cand.push_back( std::make_pair( dx*dx + dy*dy, j ) );
    }

    int k = fNearNumMax;
    if( k > (int)cand.size() ) k = (int)cand.size();
    if( k > 0 )
    {
      if( k < (int)cand.size() )
        std::nth_element( cand.begin(), cand.begin() + k, cand.end() );
      std::sort( cand.begin(), cand.begin() + k );
    }

    fNearCity[ i ][ 0 ] = i;
    fEdgeDisOrder[ i ][ 0 ] = 0;
    for( int j1 = 1; j1 <= fNearNumMax; ++j1 )
    {
      if( j1 <= k )
      {
        int c = cand[ j1-1 ].second;
        fNearCity[ i ][ j1 ] = c;
        fEdgeDisOrder[ i ][ j1 ] = this->Direct( i, c );
      }
      else
      {
        // Defensive fallback; not reached for the TSP instances used here.
        fNearCity[ i ][ j1 ] = i;
        fEdgeDisOrder[ i ][ j1 ] = 0;
      }
    }
  }
}


void TEvaluator::DoIt( TIndi& indi )
{
  int d;
  d = 0;  
  for(int i = 0; i < Ncity; ++i )
  {  
    d = d + this->Direct( i, indi.fLink[i][0] );
    d = d + this->Direct( i, indi.fLink[i][1] );
  }
  indi.fEvaluationValue = d/2;
}


int TEvaluator::Direct( int i, int j )  // Large
{
  int d, t;
  double r;

  /*
  if( strcmp( fType, "EUC_2D" ) == 0  ) {
    d = (int)(sqrt((x[i]-x[j])*(x[i]-x[j])+(y[i]-y[j])*(y[i]-y[j]))+0.5);
  }
  else if( strcmp( fType, "CEIL_2D" ) == 0  ) {  
    d = (int)ceil(sqrt((x[i]-x[j])*(x[i]-x[j])+(y[i]-y[j])*(y[i]-y[j])));
  }
  else if( strcmp( fType, "ATT" ) == 0  ) { 
    r = (sqrt(((x[i]-x[j])*(x[i]-x[j])+(y[i]-y[j])*(y[i]-y[j]))/10.0));
    t = (int)r;
     if( (double)t < r ) d = t+1;
    else d = t; 
  }
  */

  /* If EUC_2D is not used, plese write an distance function you want to use.
     This is because the use of ''if statement'' described above is time-consuming. */


  // EUC_2D:
  d = (int)(sqrt((x[i]-x[j])*(x[i]-x[j])+(y[i]-y[j])*(y[i]-y[j]))+0.5); 

  // CEIL_2D:
  // d = (int)ceil(sqrt((x[i]-x[j])*(x[i]-x[j])+(y[i]-y[j])*(y[i]-y[j])));

  // ATT:
  /*
  r = (sqrt(((x[i]-x[j])*(x[i]-x[j])+(y[i]-y[j])*(y[i]-y[j]))/10.0));
    t = (int)r;
     if( (double)t < r ) d = t+1;
    else d = t; 
  */

  return d;
}


void TEvaluator::TranceLinkOrder( TIndi& indi )  // Large
{
  int a, b;
  for(int i = 0; i < Ncity; ++i )
  {  
    a = i;
    b = indi.fLink[ i ][ 0 ];
    indi.fOrder[ i ][ 0 ] = this->GetOrd( a, b );

    b = indi.fLink[ i ][ 1 ];
    indi.fOrder[ i ][ 1 ] = this->GetOrd( a, b );
  }
}


int TEvaluator::GetOrd( int a, int b )  // Large
{
  for( int s = 0; s <= fNearNumMax; ++s )
  {
    if( fNearCity[ a ][ s ] == b )
      return s;
  }
  return -1;
}


void TEvaluator::WriteTo( FILE* fp, TIndi& indi ) 
{
  assert( Ncity == indi.fN );
  int Array[ Ncity ];
  int curr, next, pre, st, count;

  count = 0;
  pre = -1;
  curr = 0;
  st = 0;
  while( 1 )
  {
    Array[ count++ ] = curr + 1;

    if( count > Ncity ){
      printf( "Invalid\n" );
      return;
    } 
 
    if( indi.fLink[ curr ][ 0 ] == pre )
      next = indi.fLink[ curr ][ 1 ];
    else 
      next = indi.fLink[ curr ][ 0 ];

    pre = curr;
    curr = next;
    if( curr == st )
      break;
  }

  if( this->CheckValid( Array, indi.fEvaluationValue ) == false ){
    printf( "Individual is invalid \n" );
  }

  fprintf( fp, "%d %d\n", indi.fN, indi.fEvaluationValue );
  for( int i = 0; i < indi.fN; ++i )
    fprintf( fp, "%d ", Array[ i ] );
  fprintf( fp, "\n" ); 
}


bool TEvaluator::ReadFrom( FILE* fp, TIndi& indi )
{
  assert( Ncity == indi.fN );
  int Array[ Ncity ];
  int curr, next, pre, st, count;
  int N, value;

  if( fscanf( fp, "%d %d", &N, &value ) == EOF ) 
    return false;
  assert( N == Ncity );
  indi.fN = N;
  indi.fEvaluationValue = value;

  for( int i = 0; i < Ncity; ++i ){ 
    if( fscanf( fp, "%d", &Array[ i ] ) == EOF )
      return false;
  }

  if( this->CheckValid( Array, indi.fEvaluationValue ) == false ){
    printf( "Individual is invalid \n" );
    return false;
  }

  for( int i = 0; i < Ncity; ++i ){ 
    Array[ i ] -= 1; 
  }

  for( int i = 1; i < Ncity-1; ++i ){ 
    indi.fLink[ Array[ i ] ][ 0 ] = Array[ i-1 ]; 
    indi.fLink[ Array[ i ] ][ 1 ] = Array[ i+1 ]; 
  }
  indi.fLink[ Array[ 0 ] ][ 0 ] = Array[ Ncity-1 ]; 
  indi.fLink[ Array[ 0 ] ][ 1 ] = Array[ 1 ]; 
  indi.fLink[ Array[ Ncity-1 ] ][ 0 ] = Array[ Ncity-2 ]; 
  indi.fLink[ Array[ Ncity-1 ] ][ 1 ] = Array[ 0 ]; 

  return true;
}    


bool TEvaluator::CheckValid( int* array, int value )
{
  int check[ Ncity ];
  int distance;

  for( int i = 0; i < Ncity; ++i ){
    check[ i ] = 0;
  }

  for( int i = 0; i < Ncity; ++i )
    ++check[ array[ i ]-1 ];

  for( int i = 0; i < Ncity; ++i ){
    if( check[ i ] != 1 ){
      return false;
    }
  }
    
  distance = 0;  
  for( int i = 0; i < Ncity-1; ++i ){
    distance += this->Direct( array[ i ]-1, array[ i+1 ]-1 );   // Large
  }
  distance += this->Direct( array[ Ncity-1 ]-1, array[ 0 ]-1 ); // Large
  if( distance != value ){
    return false;
  }
  return true;
}
