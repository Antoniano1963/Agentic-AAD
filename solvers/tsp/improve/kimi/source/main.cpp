#include "convergence.h"
EaxConvergence eax_curve;
#ifndef __ENVIRONMENT__
#include "env.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>


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
  double avgConvDur = -1.0;
  for(int n=0; eax_curve.total_mode ? n < 11 : n < maxNumOfTrial; ++n) {
    if(eax_curve.total_mode && eax_curve.expired()) break;
    if(n>0) eax_curve.begin(dstFile,n);
    bool partial=false;
    double searchStart = eax_curve.total_mode ? eax_curve.elapsed() : 0.0;
    try { gEnv->DoIt(); }
    catch(const EaxTotalDeadline&) { partial=true; }
    if(!haveBest || gEnv->tBest.fEvaluationValue<globalBest.fEvaluationValue) {
      globalBest=gEnv->tBest; haveBest=true;
    }
    ++completed;
    if(!partial) gEnv->PrintOn(n,dstFile);
    if(!eax_curve.total_mode) gEnv->WriteBest(dstFile);
    if(eax_curve.total_mode && !partial) {
      double dur = eax_curve.elapsed() - searchStart;
      avgConvDur = (avgConvDur < 0.0) ? dur : 0.5 * (avgConvDur + dur);
      const char* blim = std::getenv("EAX_TOTAL_TIME_LIMIT");
      double budget = blim ? atof(blim) : 0.0;
      double remaining = budget - eax_curve.elapsed();
      int left = 6 - completed;
      if(left > 0 && remaining > 1.0 && avgConvDur > 0.0) {
        double target = remaining / left;
        long np = lround(gEnv->fNumOfPop * target / avgConvDur);
        long hi = gEnv->fNumOfPop + gEnv->fNumOfPop / 2; if(hi > 300) hi = 300;
        if(np < gEnv->fNumOfPop) np = gEnv->fNumOfPop;
        if(np > hi) np = hi;
        if(np < 25) np = 25;
        gEnv->fNumOfPop = (int)np;
        fprintf(stderr, "adapop: search=%d dur=%.1fs avg=%.1fs remaining=%.0fs left=%d -> pop=%d\n",
                completed, dur, avgConvDur, remaining, left, gEnv->fNumOfPop);
      }
    }
  }
  if(eax_curve.total_mode) {
    eax_curve.record(0,0,"total_time_limit",true);
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
