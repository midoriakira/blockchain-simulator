// Direct broadcast, heterogeneous directed delays, first-seen ties.
// A is miner 0. Other miners have equal hash shares. No forwarding is added.
// All block records are retained. O deliveries are reconciled lazily on mining.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <deque>
#include <iomanip>
#include <iostream>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

struct Block { int parent,height,owner; long double time; };
struct Delivery { long double time; int id; };
struct GroupNetwork {
    std::vector<Block> blocks{{0,0,-1,0}};
    std::vector<int> tips;
    std::array<std::deque<Delivery>,3> queues; // O->A, O->O, A->O
    long double out_delay,in_delay,small_delay;
    int height=0,peak=0,public_O_tip=0;
    int effective_tip(int j)const{
        return j>0 && blocks[tips[j]].height<blocks[public_O_tip].height?public_O_tip:tips[j];
    }
    GroupNetwork(int n,long double out,long double in,long double small):
        tips(n,0),out_delay(out),in_delay(in),small_delay(small){}
    void flush(long double time){
        for(;;){
            int next=-1;
            for(int k=0;k<3;++k)if(!queues[k].empty() && queues[k].front().time<=time)
                if(next<0 || std::tie(queues[k].front().time,queues[k].front().id)<
                             std::tie(queues[next].front().time,queues[next].front().id))next=k;
            if(next<0)break;
            int id=queues[next].front().id;queues[next].pop_front();
            if(next==0){if(blocks[id].height>blocks[tips[0]].height)tips[0]=id;}
            else if(blocks[id].height>blocks[public_O_tip].height)public_O_tip=id;
        }
    }
    int mine(long double time,int owner){
        flush(time);tips.at(owner)=effective_tip(owner);int parent=tips.at(owner),id=blocks.size();
        blocks.push_back({parent,blocks[parent].height+1,owner,time});tips[owner]=id;
        if(owner==0)queues[2].push_back({time+out_delay,id});
        else{queues[0].push_back({time+in_delay,id});queues[1].push_back({time+small_delay,id});}
        if(blocks.back().height>height){assert(blocks.back().height==height+1);++height;peak=id;}
        return id;
    }
    std::vector<int> path(int tip)const{
        std::vector<int> ids;
        while(tip){ids.push_back(tip);int parent=blocks[tip].parent;
            assert(blocks[parent].height==blocks[tip].height-1);tip=parent;}
        return ids;
    }
};

// Independent per-recipient heap, used only for differential tests.
struct HeapNetwork {
    using Event=std::tuple<long double,int,int>;
    std::priority_queue<Event,std::vector<Event>,std::greater<Event>> pending;
    std::vector<Block> blocks{{0,0,-1,0}};
    std::vector<int> tips;
    long double out_delay,in_delay,small_delay;
    HeapNetwork(int n,long double out,long double in,long double small):
        tips(n,0),out_delay(out),in_delay(in),small_delay(small){}
    void mine(long double time,int owner){
        while(!pending.empty() && std::get<0>(pending.top())<=time){
            auto [at,id,to]=pending.top();pending.pop();
            if(blocks[id].height>blocks[tips[to]].height)tips[to]=id;
        }
        int parent=tips.at(owner),id=blocks.size();
        blocks.push_back({parent,blocks[parent].height+1,owner,time});tips[owner]=id;
        for(int j=0;j<(int)tips.size();++j)if(j!=owner){
            long double delay=owner==0?out_delay:(j==0?in_delay:small_delay);
            pending.emplace(time+delay,id,j);
        }
    }
};

void test(){
    int checked=0;
    for(int n:{2,10,100})for(auto delays:std::vector<std::array<long double,3>>{
        {0,0,0},{1,1,1},{.1L,1,10},{10,.1L,1},{1,10,.1L}}){
        GroupNetwork group(n,delays[0],delays[1],delays[2]);
        HeapNetwork heap(n,delays[0],delays[1],delays[2]);
        std::mt19937_64 rng(10);std::exponential_distribution<double> gap(1);
        std::uniform_int_distribution<int> miner(0,n-1);long double time=0;
        for(int i=0;i<12000;++i){time+=gap(rng);int m=miner(rng);
            group.mine(time,m);heap.mine(time,m);
            assert(group.blocks.back().parent==heap.blocks.back().parent);
            assert(group.blocks.back().height==heap.blocks.back().height);
            for(int j=0;j<n;++j)assert(group.effective_tip(j)==heap.tips[j]);
            ++checked;
        }
    }
    for(bool fast:{false,true}){
        GroupNetwork send(3,fast?.1L:1,1,1);send.mine(0,0);send.mine(.5L,1);
        assert(send.blocks.back().parent==(fast?1:0));
        GroupNetwork receive(3,1,fast?.1L:1,1);receive.mine(0,1);receive.mine(.5L,0);
        assert(receive.blocks.back().parent==(fast?1:0));
        GroupNetwork small(3,1,1,fast?.1L:1);small.mine(0,1);small.mine(.5L,2);
        assert(small.blocks.back().parent==(fast?1:0));
    }
    GroupNetwork two_fast(2,1,1,0),two_slow(2,1,1,1000);
    std::mt19937_64 rng(100);std::exponential_distribution<double> gap(1);
    std::uniform_int_distribution<int> miner(0,1);long double time=0;
    for(int i=0;i<5000;++i){time+=gap(rng);int owner=miner(rng);
        two_fast.mine(time,owner);two_slow.mine(time,owner);
        assert(two_fast.blocks.back().parent==two_slow.blocks.back().parent);
    }
    std::cout<<"{\"differential_blocks\":"<<checked<<",\"delay_role_checks\":6,\"two_node_small_delay_invariance_blocks\":5000,\"status\":\"passed\"}\n";
}

void json_ratio(long long numerator,long long denominator){
    if(denominator)std::cout<<(double)numerator/denominator;
    else std::cout<<"null";
}

int main(int argc,char**argv){
    try{
        if(argc==2 && std::string(argv[1])=="test"){test();return 0;}
        if(argc!=10){std::cerr<<"nodes alpha out_delay in_delay small_delay end_time burn_time lookahead_time seed\n";return 2;}
        int n=std::stoi(argv[1]);double alpha=std::stod(argv[2]);
        long double out=std::stold(argv[3]),in=std::stold(argv[4]),small=std::stold(argv[5]);
        long double end=std::stold(argv[6]),burn=std::stold(argv[7]),lookahead=std::stold(argv[8]);
        auto seed=std::stoull(argv[9]);
        if(n<2 || !(alpha>0 && alpha<1) || !std::isfinite(alpha)
           || !std::isfinite(out) || !std::isfinite(in) || !std::isfinite(small)
           || !std::isfinite(end) || !std::isfinite(burn) || !std::isfinite(lookahead)
           || !std::isfinite(end+2*lookahead)
           || std::min({out,in,small})<0 || end<=burn || burn<0 || lookahead<=0)
            throw std::runtime_error("invalid parameters");
        GroupNetwork world(n,out,in,small);std::mt19937_64 rng(seed);
        std::exponential_distribution<double> gap(1);std::uniform_real_distribution<double> choice(0,1);
        long double time=0;std::vector<int> snapshot,confirmation;bool snapped=false,confirmed=false;int generated_A=0;
        for(;;){
            time+=gap(rng);
            if(!snapped && time>end){world.flush(end);snapshot=world.path(world.peak);snapped=true;}
            if(!confirmed && time>end+lookahead){world.flush(end+lookahead);confirmation=world.path(world.peak);confirmed=true;}
            if(time>end+2*lookahead)break;
            double u=choice(rng);
            int owner=u<alpha?0:1+std::min(n-2,(int)((u-alpha)/(1-alpha)*(n-1)));
            world.mine(time,owner);
            if(time>burn && time<=end && owner==0)++generated_A;
        }
        auto selected=world.path(world.peak);std::vector<bool> after(world.blocks.size(),false);
        long long accepted_A=0,accepted_all=0,changed=0,confirmation_changed=0;
        for(int id:selected){after[id]=true;const auto&b=world.blocks[id];
            if(b.time>burn && b.time<=end){++accepted_all;accepted_A+=b.owner==0;}}
        for(int id:snapshot){const auto&b=world.blocks[id];
            if(b.time>burn && b.time<=end && !after[id])++changed;}
        for(int id:confirmation){const auto&b=world.blocks[id];
            if(b.time>burn && b.time<=end && !after[id])++confirmation_changed;}
        double duration=end-burn;
        std::cout<<std::setprecision(17)<<"{\"nodes\":"<<n<<",\"alpha\":"<<alpha
                 <<",\"out_delay\":"<<(double)out<<",\"in_delay\":"<<(double)in
                 <<",\"small_delay\":"<<(double)small<<",\"end_time\":"<<(double)end
                 <<",\"burn_time\":"<<(double)burn<<",\"lookahead_time\":"<<(double)lookahead
                 <<",\"seed\":"<<seed<<",\"generated_A\":"<<generated_A
                 <<",\"accepted_A\":"<<accepted_A<<",\"accepted_all\":"<<accepted_all
                 <<",\"reward_share_A\":";
        json_ratio(accepted_A,accepted_all);
        std::cout<<",\"reward_rate_A\":"<<accepted_A/duration
                 <<",\"acceptance_A\":";
        json_ratio(accepted_A,generated_A);
        std::cout<<",\"chain_rate\":"<<accepted_all/duration
                 <<",\"snapshot_blocks_reorganized\":"<<changed
                 <<",\"confirmation_blocks_reorganized\":"<<confirmation_changed<<"}\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
    return 0;
}
