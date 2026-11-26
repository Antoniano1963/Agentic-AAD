#ifndef EAX_CONVERGENCE_H
#define EAX_CONVERGENCE_H
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <climits>
#include <string>
#include <stdexcept>

// Observer state is independent of population, stopping counters and RNG.
struct EaxTotalDeadline {};
struct EaxConvergence {
    bool total_mode = false;
    std::chrono::steady_clock::time_point start;
    FILE* file = NULL;
    double bks = 0, limit = 0, interval = 1, last = -1;
    int best = INT_MAX, trial = 0;
    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    }
    static double option(const char* name, double fallback) {
        const char* value = std::getenv(name);
        if (!value) return fallback;
        char* end = NULL;
        double number = std::strtod(value, &end);
        if (!end || *end || !std::isfinite(number) || number <= 0)
            throw std::runtime_error(std::string("Invalid positive option: ")+name);
        return number;
    }
    void begin(const char* prefix, int index) {
        if(index>0 && total_mode) { trial=index; record(0,0,"restart",true); return; }
        start = std::chrono::steady_clock::now();
        trial=index; best=INT_MAX; last=-1;
        bks=option("EAX_BKS",0);
        total_mode = std::getenv("EAX_TOTAL_TIME_LIMIT") != NULL;
        if(total_mode && std::getenv("EAX_TIME_LIMIT")) throw std::runtime_error("Use either EAX_TOTAL_TIME_LIMIT or EAX_TIME_LIMIT");
        limit=option(total_mode ? "EAX_TOTAL_TIME_LIMIT" : "EAX_TIME_LIMIT",0);
        interval=option("EAX_LOG_INTERVAL",1);
        if (interval>1) throw std::runtime_error("EAX_LOG_INTERVAL must be <= 1 second");
        std::string path=std::string(prefix)+(total_mode ? "_convergence_total.csv" : "_convergence_"+std::to_string(index)+".csv");
        file=std::fopen(path.c_str(),"w");
        if (!file) throw std::runtime_error("Cannot create convergence CSV");
        std::fprintf(file,"trial,time_seconds,generation,stage,event,best_length,gap_percent,population_best,population_mean\n");
        record(0,0,"start",true);
    }
    bool expired() const { return limit>0 && elapsed()>=limit; }
    void record(int generation,int stage,const char* event,bool force,
                int population_best=0,double population_mean=0) {
        double now=elapsed();
        if (!force && last>=0 && now-last<interval) return;
        std::fprintf(file,"%d,%.9f,%d,%d,%s,",trial,now,generation,stage,event);
        if (best!=INT_MAX) std::fprintf(file,"%d",best);
        std::fprintf(file,",");
        if (best!=INT_MAX && bks>0) std::fprintf(file,"%.12f",100.0*(best-bks)/bks);
        std::fprintf(file,",");
        if (population_best>0) std::fprintf(file,"%d",population_best);
        std::fprintf(file,",");
        if (population_mean>0) std::fprintf(file,"%.9f",population_mean);
        std::fprintf(file,"\n");
        std::fflush(file);
        last=now;
    }
    void observe(int value,int generation,int stage,const char* event) {
        bool improved=value<best;
        if(improved) best=value;
        record(generation,stage,improved?"improvement":event,improved);
    }
    void finish(int generation,int stage,const char* reason) {
        record(generation,stage,reason,true);
        if(!total_mode) close();
    }
    void close() { if(file) { std::fclose(file); file=NULL; } }
};
extern EaxConvergence eax_curve;
#endif
