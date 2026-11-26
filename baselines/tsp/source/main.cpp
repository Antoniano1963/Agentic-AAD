#include "convergence.h"
EaxConvergence eax_curve;
#ifndef __ENVIRONMENT__
#include "env.h"
#endif

#include <stdio.h>
#include <stdlib.h>


int main( int argc, char* argv[] )
{
  if(argc < 6 || argc > 7) { fprintf(stderr, "Usage: trials prefix population offspring instance [initial_population]\n"); return 2; }
  int maxNumOfTrial;
  
  sscanf( argv[1], "%d", &maxNumOfTrial );
  char* dstFile = argv[2];
  
  TEnvironment* gEnv = NULL;
  gEnv = new TEnvironment();
  const char* seed = getenv("EAX_SEED");
  if(seed) InitURandom(atoi(seed)); else InitURandom();  

  int d;
  sscanf( argv[3], "%d", &d );
  gEnv->fNumOfPop = d;
  sscanf( argv[4], "%d", &d );
  gEnv->fNumOfKids = d;
  gEnv->fFileNameTSP = argv[5];
  gEnv->fFileNameInitPop = NULL;
  if( argc == 7 )
    gEnv->fFileNameInitPop = argv[6];

  eax_curve.begin(dstFile, 0);
  gEnv->Define();
  
  // In total-budget mode the trial count is replaced by deadline-driven restarts.
  if(eax_curve.total_mode && gEnv->fFileNameInitPop) {
    fprintf(stderr,"Total-budget mode requires random population initialization\n"); return 2;
  }
  TIndi globalBest;
  globalBest.Define(gEnv->fEvaluator->Ncity);
  bool haveBest=false;
  int completed=0;
  int maxRestarts=2147483646;
  if(const char* value=getenv("EAX_MAX_RESTARTS")) {
    char* end=NULL; long parsed=strtol(value,&end,10);
    if(!*value || *end || parsed<0 || parsed>2147483646) return 2;
    maxRestarts=(int)parsed;
  }
  for(int n=0; (eax_curve.total_mode ? n<=maxRestarts : n<maxNumOfTrial); ++n) {
    if(eax_curve.total_mode && eax_curve.expired()) break;
    if(n>0) eax_curve.begin(dstFile,n);
    bool partial=false;
    try { gEnv->DoIt(); }
    catch(const EaxTotalDeadline&) { partial=true; }
    if(!haveBest || gEnv->tBest.fEvaluationValue<globalBest.fEvaluationValue) {
      globalBest=gEnv->tBest; haveBest=true;
    }
    ++completed;
    if(!partial) gEnv->PrintOn(n,dstFile);
    if(!eax_curve.total_mode) gEnv->WriteBest(dstFile);
  }
  if(eax_curve.total_mode) {
    eax_curve.record(0,0,eax_curve.expired()?"total_time_limit":"restart_limit",true);
    if(haveBest) { gEnv->tBest=globalBest; gEnv->WriteBest(dstFile); }
    std::string path=std::string(dstFile)+"_total.json";
    FILE* meta=fopen(path.c_str(),"w");
    if(!meta) return 3;
    fprintf(meta,"{\"searches_started\":%d,\"elapsed_seconds\":%.9f,\"best_length\":%d}\n",
      completed,eax_curve.elapsed(),haveBest?globalBest.fEvaluationValue:-1);
    fclose(meta); eax_curve.close();
    if(!haveBest) return 4;
  }

  return 0;
}
