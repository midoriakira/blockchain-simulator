// Compare independent common-delay processing with lazy grouped delivery.
#define main uniform_program_main
#include "../src/uniform_simulator.cpp"
#undef main
#define main latency_program_main
#include "../src/latency_simulator.cpp"
#undef main

int main(){
    long long checked=0;
    for(int n:{2,10,100})for(long double delta:{0.L,.1L,10.L}){
        World uniform(n,delta);GroupNetwork directed(n,delta,delta,delta);
        std::mt19937_64 rng(10);std::exponential_distribution<double> gap(1);
        std::uniform_int_distribution<int> miner(0,n-1);long double time=0;
        for(int i=0;i<12000;++i){
            time+=gap(rng);int owner=miner(rng);
            uniform.mine(time,owner);directed.mine(time,owner);
            assert(uniform.blocks.back().parent==directed.blocks.back().parent);
            assert(uniform.blocks.back().height==directed.blocks.back().height);
            for(int j=0;j<n;++j)assert(uniform.tips[j]==directed.effective_tip(j));
            ++checked;
        }
    }
    std::cout<<"{\"uniform_directed_parent_checks\":"<<checked<<",\"status\":\"passed\"}\n";
}
