// Reuse the validated engine and reference checker; preserve historical harness.
#define main historical_utilization_main
#include "utilization_bench.cpp"
#undef main
#include <algorithm>
int main(int argc, char** argv) {
 try {
  auto arg=[&](std::string k,std::string d){for(int i=1;i+1<argc;++i)if(argv[i]==k)return std::string(argv[i+1]);return d;};
  std::string policy=arg("--policy","bfc");
  if(policy!="bfc"&&policy!="direct_every"&&policy!="direct_idle"&&policy!="pool_idle")throw std::runtime_error("unknown policy");
  int frames=std::stoi(arg("--frames","120")), burst=std::stoi(arg("--burst","30")), pause=std::stoi(arg("--pause-ms","200"));
  if(frames<1||burst<1||pause<0)throw std::runtime_error("invalid protocol");
  cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(2,&mask);if(sched_setaffinity(0,sizeof(mask),&mask))throw std::runtime_error("affinity failed");
  auto refs=read_reference(arg("--reference","first_steps/bench/utilization/results/recheck/reference.bin"));
  Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"activity");
  Engine e(env,arg("--model","first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx"),policy=="bfc"?"bfc":policy=="pool_idle"?"pool_retain":"direct",true);
  auto reclaim=[&](){if(e.allocator&&e.allocator->pool)e.allocator->pool->purge();malloc_trim(0);};
  double error=0;for(int i=0;i<10;++i){auto o=e.run(i%8);verify(o,refs[i%8],error);} 
  std::vector<double> latency;double byte_seconds=0;size_t idle_private=0;long faults=0;int purges=0;
  std::vector<Ort::Value> held;int held_seed=0;
  rusage before{};getrusage(RUSAGE_SELF,&before);
  for(int i=0;i<frames;++i){
   // Time inference, output release and reclamation; verification and /proc excluded.
   auto t=std::chrono::steady_clock::now();auto o=e.run(i%8);
   double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
   verify(o,refs[i%8],error);if(!held.empty())verify(held,refs[held_seed],error);
   t=std::chrono::steady_clock::now();
   if(i%11==5)held.clear();if(i%11==0){held=std::move(o);held_seed=i%8;}o.clear();
   if(policy=="direct_every"){reclaim();++purges;}
   sec+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();latency.push_back(sec);
   auto s=snapshot();byte_seconds+=s.private_bytes*sec;
   if((i+1)%burst==0&&pause){
    t=std::chrono::steady_clock::now();if(policy=="direct_idle"||policy=="pool_idle"){reclaim();++purges;}
    double cost=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
    auto idle=snapshot();idle_private=idle.private_bytes;byte_seconds+=s.private_bytes*cost+idle.private_bytes*(pause/1000.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(pause));
   }
  }
  if(!held.empty())verify(held,refs[held_seed],error);rusage after{};getrusage(RUSAGE_SELF,&after);faults=after.ru_minflt-before.ru_minflt;
  double total=std::accumulate(latency.begin(),latency.end(),0.0);std::sort(latency.begin(),latency.end());
  std::cout<<"{\"policy\":\""<<policy<<"\",\"frames\":"<<frames<<",\"pause_ms\":"<<pause<<",\"burst\":"<<burst<<",\"max_abs_error\":"<<error<<",\"fps\":"<<frames/total<<",\"p99_ms\":"<<latency[std::min(latency.size()-1,size_t(std::ceil(latency.size()*.99)-1))]*1000<<",\"minor_faults\":"<<faults<<",\"reclaim_calls\":"<<purges<<",\"idle_private_bytes\":"<<idle_private<<",\"estimated_private_byte_seconds\":"<<byte_seconds<<"}\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
