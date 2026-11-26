#include <climits>
#pragma once
// Small, independently implemented linear-model adapter for the S46 source.
// This is not IBM Concert. Solver-specific search/pool semantics differ.
#include <memory>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <cmath>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <atomic>
#ifdef _OPENMP
#include <omp.h>
#endif
// v004: pool of persistent bridge workers, one per OMP lane, created by the
// main thread before any parallel region (avoids fork inside OMP threads).
// inline (C++17) so all translation units share one pool
inline const int MILP_MAX_LANES=64;
inline FILE* g_milp_pool_req[MILP_MAX_LANES]={nullptr};
inline FILE* g_milp_pool_resp[MILP_MAX_LANES]={nullptr};
inline int g_milp_pool_size=0;
inline void MILP_init_workers(int n){
  const char* bridge=getenv("OPEN_MILP_BRIDGE");if(!bridge)throw std::runtime_error("OPEN_MILP_BRIDGE required");
  if(n>MILP_MAX_LANES)n=MILP_MAX_LANES;
  for(int i=0;i<n;i++){
    if(g_milp_pool_req[i])continue;
    int inpipe[2],outpipe[2];
    if(pipe(inpipe)||pipe(outpipe)) throw std::runtime_error("MILP pipe failed");
    for(int k=0;k<2;k++){fcntl(inpipe[k],F_SETFD,FD_CLOEXEC);fcntl(outpipe[k],F_SETFD,FD_CLOEXEC);}
    pid_t worker=fork();
    if(worker<0) throw std::runtime_error("MILP fork failed");
    if(worker==0){
      dup2(inpipe[0],STDIN_FILENO);dup2(outpipe[1],STDOUT_FILENO);
      close(inpipe[0]);close(inpipe[1]);close(outpipe[0]);close(outpipe[1]);
      std::string server=std::string(bridge)+".server.py";
      execlp("python3","python3",server.c_str(),bridge,(char*)nullptr);_exit(127);
    }
    close(inpipe[0]);close(outpipe[1]);
    g_milp_pool_req[i]=fdopen(inpipe[1],"w");g_milp_pool_resp[i]=fdopen(outpipe[0],"r");
    if(!g_milp_pool_req[i]||!g_milp_pool_resp[i]) throw std::runtime_error("MILP fdopen failed");
  }
  g_milp_pool_size=n;
}
#define ILOSTLBEGIN
using IloInt=int; using IloBool=bool;
constexpr double IloInfinity=1e20;
enum {ILOFLOAT=0,ILOINT=1,ILOBOOL=2};
struct IloEnv { void end(){} std::ostream& getNullStream(){static std::ofstream f("/dev/null");return f;} };
struct OV {double lb=0,ub=IloInfinity;int type=0;std::string name;};
struct IloNumVar {
 std::shared_ptr<OV> p;
 IloNumVar(){} IloNumVar(IloEnv&,double l=0,double u=IloInfinity,int t=ILOFLOAT):p(std::make_shared<OV>()){p->lb=l;p->ub=u;p->type=t;}
 void setName(const char*n){p->name=n;} const char* getName()const{return p->name.c_str();}
 void setBounds(double l,double u){p->lb=l;p->ub=u;}void setLB(double l){p->lb=l;}void setUB(double u){p->ub=u;}
 double getLB()const{return p->lb;}double getUB()const{return p->ub;}
};
struct IloIntVar:IloNumVar {IloIntVar(){} IloIntVar(IloEnv&e,double l=0,double u=IloInfinity):IloNumVar(e,l,u,ILOINT){} };
template<class T>struct IloArray {
 std::shared_ptr<std::vector<T>> p=std::make_shared<std::vector<T>>();
 IloArray(){} IloArray(IloEnv&,size_t n=0):p(std::make_shared<std::vector<T>>(n)){}
 T& operator[](size_t i){return p->at(i);}const T& operator[](size_t i)const{return p->at(i);}
 int getSize()const{return p->size();} void add(const T&v){p->push_back(v);}void end(){p=std::make_shared<std::vector<T>>();}
};
struct IloNumVarArray:IloArray<IloNumVar>{using IloArray::IloArray;
 IloNumVarArray(IloEnv&e,int n,double l,double u,int t=ILOFLOAT){for(int i=0;i<n;i++)add(IloNumVar(e,l,u,t));}
};
struct IloIntVarArray:IloNumVarArray{using IloNumVarArray::IloNumVarArray;
 IloIntVarArray(IloEnv&e,int n,double l,double u){for(int i=0;i<n;i++)add(IloIntVar(e,l,u));}
};
struct IloBoolVarArray:IloNumVarArray{IloBoolVarArray(){}IloBoolVarArray(IloEnv&e,int n=0){for(int i=0;i<n;i++)add(IloNumVar(e,0,1,ILOBOOL));}};
using IloNumArray=IloArray<double>;
struct IloExpr {
 std::map<std::shared_ptr<OV>,double> a;double c=0;
 IloExpr(){}IloExpr(IloEnv&){}IloExpr(double x):c(x){}IloExpr(const IloNumVar&v){if(!v.p)throw std::runtime_error("null var");a[v.p]=1;}
 IloExpr& operator+=(const IloExpr&b){c+=b.c;for(auto&x:b.a)a[x.first]+=x.second;return *this;}
 IloExpr& operator-=(const IloExpr&b){c-=b.c;for(auto&x:b.a)a[x.first]-=x.second;return *this;}
 void end(){a.clear();c=0;}
};
inline IloExpr operator+(IloExpr a,const IloExpr&b){return a+=b;}
inline IloExpr operator-(IloExpr a,const IloExpr&b){return a-=b;}
inline IloExpr operator*(double k,IloExpr a){a.c*=k;for(auto&x:a.a)x.second*=k;return a;}
inline IloExpr operator*(IloExpr a,double k){return k*a;}
inline IloExpr operator/(IloExpr a,double k){return (1/k)*a;}
struct ORow{double l=-IloInfinity,u=IloInfinity;IloExpr e;std::string name;};
struct IloRange{std::shared_ptr<ORow>p=std::make_shared<ORow>();IloRange(){}IloRange(IloEnv&,double lb,const IloExpr&x,double ub,const char*n=""){p->l=lb;p->u=ub;p->e=x;p->name=n;}void setName(const char*n){p->name=n;}const char*getName()const{return p->name.empty()?nullptr:p->name.c_str();}void setLB(double x){p->l=x;}void setUB(double x){p->u=x;}IloExpr getExpr()const{return p->e;}void setLinearCoef(const IloNumVar&v,double x){p->e.a[v.p]=x;}};
using IloRangeArray=IloArray<IloRange>;
inline IloRange operator<=(const IloExpr&a,const IloExpr&b){IloRange r;r.p->e=a-b;r.p->u=0;return r;}
inline IloRange operator>=(const IloExpr&a,const IloExpr&b){IloRange r;r.p->e=a-b;r.p->l=0;return r;}
inline IloRange operator==(const IloExpr&a,const IloExpr&b){IloRange r;r.p->e=a-b;r.p->l=r.p->u=0;return r;}
struct IloObjective{IloExpr e;};inline IloObjective IloMinimize(IloEnv&,const IloExpr&e){return {e};}
struct IloConversion{IloNumVar v;int t;IloConversion(IloEnv&,IloNumVar x,int y):v(x),t(y){}};
struct OM{std::vector<IloRange>rows;IloExpr obj;std::map<std::shared_ptr<OV>,int> types;std::vector<std::shared_ptr<OV>> explicit_vars;};
struct IloModel {
 std::shared_ptr<OM>p=std::make_shared<OM>();IloModel(){}IloModel(IloEnv&){}
 void add(const IloRange&r){p->rows.push_back(r);}void add(const IloRangeArray&rs){for(auto&r:*rs.p)add(r);}
 void add(const IloObjective&o){p->obj=o.e;}void add(const IloConversion&c){p->types[c.v.p]=c.t;}
 void add(const IloNumVar&v){p->explicit_vars.push_back(v.p);}void end(){p=std::make_shared<OM>();}
};
struct IloCplex {
 enum Param{Threads,TiLim,EpGap,MIPEmphasis,SolnPoolReplace,SolnPoolCapacity};enum Status{Unknown,Optimal,Feasible,Infeasible};static constexpr int MIPStartRepair=0;
 IloModel m;double tim=10,gap=.005,obj=IloInfinity,bound=-IloInfinity;int emph=0,threads=1;Status status=Unknown;
 std::map<std::shared_ptr<OV>,double> values,start;int nsol=0;
 IloCplex(){}IloCplex(const IloModel&x):m(x){}IloCplex(IloEnv&){}
 void extract(const IloModel&x){m=x;}void clearModel(){m=IloModel();values.clear();}void end(){clearModel();}
 void setOut(std::ostream&){}void setParam(Param p,double v){if(p==TiLim)tim=v;else if(p==EpGap)gap=v;else if(p==Threads)threads=v;else if(p==MIPEmphasis)emph=v;else throw std::runtime_error("Unsupported pool parameter");}
 void addMIPStart(const IloNumVarArray&v,const IloNumArray&x,int=0){for(int i=0;i<v.getSize();i++)start[v[i].p]=x[i];}
 bool isExtracted(const IloNumVar&v)const{return values.count(v.p);}
 double getValue(const IloNumVar&v,int=0)const{auto it=values.find(v.p);if(it==values.end())throw std::runtime_error("unextracted var value");return it->second;}
 void getValues(IloNumArray&x,const IloNumVarArray&v,int=0){x.end();for(int i=0;i<v.getSize();i++)x.add(getValue(v[i]));}
 double getObjValue()const{return obj;}double getBestObjValue()const{return bound;}
 double getMIPRelativeGap()const{return std::abs(obj-bound)/(1e-10+std::abs(obj));}int getSolnPoolNsolns()const{return nsol;}Status getCplexStatus()const{return status;}
 void exportModel(const char*){throw std::runtime_error("exportModel unavailable in adapter");}
 bool populate(){return solve();}
 bool solve(){
  std::map<std::shared_ptr<OV>,int> ix;auto reg=[&](const IloExpr&e){for(auto&x:e.a)if(x.second!=0&&!ix.count(x.first)){int n=ix.size();ix[x.first]=n;}};
  reg(m.p->obj);for(auto&r:m.p->rows)reg(r.p->e);for(auto&v:m.p->explicit_vars)if(!ix.count(v)){int n=ix.size();ix[v]=n;}
  std::vector<std::shared_ptr<OV>> vs(ix.size());for(auto&x:ix)vs[x.second]=x.first;
  static std::atomic<unsigned> counter{0};std::string stem="milp_"+std::to_string(getpid())+"_"+std::to_string(counter.fetch_add(1));
  std::ofstream f(stem+".in");f<<std::setprecision(17)<<vs.size()<<' '<<m.p->rows.size()<<' '<<tim<<' '<<gap<<' '<<emph<<' '<<threads<<' '<<m.p->obj.c<<'\n';
  for(auto&v:vs){int t=m.p->types.count(v)?m.p->types.at(v):v->type;double warm=start.count(v)?start.at(v):(values.count(v)?values.at(v):1e100);f<<v->lb<<' '<<v->ub<<' '<<t<<' '<<m.p->obj.a[v]<<' '<<warm<<'\n';}
  for(auto&r:m.p->rows){int nz=0;for(auto&x:r.p->e.a)if(x.second!=0)nz++;f<<r.p->l-r.p->e.c<<' '<<r.p->u-r.p->e.c<<' '<<nz;for(auto&x:r.p->e.a)if(x.second!=0)f<<' '<<ix.at(x.first)<<' '<<x.second;f<<'\n';}f.close();
  const char* bridge=getenv("OPEN_MILP_BRIDGE");if(!bridge)throw std::runtime_error("OPEN_MILP_BRIDGE required");
  // Per-lane persistent workers, pre-created by MILP_init_workers() from the
  // main thread. solve() only selects the lane of the calling OMP thread.
  int lane=0;
#ifdef _OPENMP
  if(g_milp_pool_size>1) lane=omp_get_thread_num();
  if(lane>=g_milp_pool_size) lane=0;
#endif
  FILE* request=g_milp_pool_req[lane]; FILE* response=g_milp_pool_resp[lane];
  if(!request){
    if(g_milp_pool_size==0) MILP_init_workers(1); // serial fallback
    request=g_milp_pool_req[0]; response=g_milp_pool_resp[0];
  }
  if(fprintf(request,"%s\t%s\n",(stem+".in").c_str(),(stem+".out").c_str())<0 || fflush(request)!=0)
    throw std::runtime_error("MILP request failed");
  char ack[32];
  if(!fgets(ack,sizeof(ack),response)||std::string(ack)!="OK\n") throw std::runtime_error("MILP worker failed");
  std::ifstream g(stem+".out");int ok,optimal;int proven_infeasible=0;g>>ok>>optimal>>obj>>bound>>proven_infeasible;nsol=ok?1:0;status=optimal?Optimal:(ok?Feasible:(proven_infeasible?Infeasible:Unknown));values.clear();
  if(ok){for(auto&v:vs){double x;g>>x;if(!g)throw std::runtime_error("truncated MILP result");values[v]=x;}}
  if(!getenv("KEEP_MILP_MODELS")){unlink((stem+".in").c_str());unlink((stem+".out").c_str());}return ok;
 }
};
