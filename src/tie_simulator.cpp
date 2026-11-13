// Random longest-tip rules. Mining and tie randomness use separate streams.
// random-own: prefer one's own block at the maximum known height; otherwise
//             choose uniformly from ALL known tips at that height.
// random-all: choose uniformly, including one's own competing block.
// Both definitions cover arbitrary multiway ties, not only two-block forks.
#define main first_seen_cli_main
#include "latency_simulator.cpp"
#undef main

struct TipPool {
    int height=0;
    std::vector<int> ids{0};
    void receive(int id,const std::vector<Block>& blocks){
        int h=blocks[id].height;
        if(h>height){height=h;ids.clear();}
        if(h==height)ids.push_back(id);
    }
};

int pick_tip(std::vector<int> candidates,int own,bool prefer_own,double u){
    std::sort(candidates.begin(),candidates.end());
    candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
    assert(!candidates.empty());
    if(prefer_own && std::binary_search(candidates.begin(),candidates.end(),own))return own;
    return candidates[std::min(candidates.size()-1,(size_t)(u*candidates.size()))];
}

struct RandomGroup {
    std::vector<Block> blocks{{0,0,-1,0}};
    std::vector<int> own;
    std::array<std::deque<Delivery>,3> queues;
    TipPool A,O;
    long double out_delay,in_delay,small_delay;
    bool prefer_own;
    int peak=0;
    RandomGroup(int n,long double out,long double in,long double small,bool preference):
        own(n,0),out_delay(out),in_delay(in),small_delay(small),prefer_own(preference){}
    void flush(long double time){
        for(;;){
            int next=-1;
            for(int k=0;k<3;++k)if(!queues[k].empty() && queues[k].front().time<=time)
                if(next<0 || std::tie(queues[k].front().time,queues[k].front().id)<
                             std::tie(queues[next].front().time,queues[next].front().id))next=k;
            if(next<0)break;
            int id=queues[next].front().id;queues[next].pop_front();
            (next==0?A:O).receive(id,blocks);
        }
    }
    std::vector<int> candidates(int owner)const{
        const auto& pool=owner==0?A:O;
        int private_id=own[owner],h=blocks[private_id].height;
        if(h>pool.height)return {private_id};
        auto ids=pool.ids;
        if(h==pool.height)ids.push_back(private_id);
        return ids;
    }
    int mine(long double time,int owner,double u){
        flush(time);
        int parent=pick_tip(candidates(owner),own[owner],prefer_own,u),id=blocks.size();
        blocks.push_back({parent,blocks[parent].height+1,owner,time});own[owner]=id;
        if(owner==0){A.receive(id,blocks);queues[2].push_back({time+out_delay,id});}
        else{queues[0].push_back({time+in_delay,id});queues[1].push_back({time+small_delay,id});}
        if(blocks[id].height>blocks[peak].height)peak=id;
        return id;
    }
    std::vector<int> path(int tip)const{
        std::vector<int> ids;
        while(tip){ids.push_back(tip);tip=blocks[tip].parent;}
        return ids;
    }
};

// Independent eager reference: one delivery heap and one candidate pool per miner.
struct RandomHeap {
    using Event=std::tuple<long double,int,int>;
    std::priority_queue<Event,std::vector<Event>,std::greater<Event>> pending;
    std::vector<Block> blocks{{0,0,-1,0}};
    std::vector<TipPool> views;
    std::vector<int> own;
    long double out_delay,in_delay,small_delay;
    bool prefer_own;
    RandomHeap(int n,long double out,long double in,long double small,bool preference):
        views(n),own(n,0),out_delay(out),in_delay(in),small_delay(small),prefer_own(preference){}
    void mine(long double time,int owner,double u){
        while(!pending.empty() && std::get<0>(pending.top())<=time){
            auto [at,id,to]=pending.top();pending.pop();views[to].receive(id,blocks);
        }
        // Reference choice does not call pick_tip: direct sorted-set sampling.
        auto ids=views[owner].ids;
        std::sort(ids.begin(),ids.end());
        ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        int parent;
        if(prefer_own && std::find(ids.begin(),ids.end(),own[owner])!=ids.end())parent=own[owner];
        else parent=ids.at(std::min(ids.size()-1,(size_t)(u*ids.size())));
        int id=blocks.size();blocks.push_back({parent,blocks[parent].height+1,owner,time});
        own[owner]=id;views[owner].receive(id,blocks);
        for(int j=0;j<(int)views.size();++j)if(j!=owner){
            long double d=owner==0?out_delay:(j==0?in_delay:small_delay);
            pending.emplace(time+d,id,j);
        }
    }
};

struct FirstSeenAdapter:GroupNetwork {
    using GroupNetwork::GroupNetwork;
    int mine(long double t,int m,double){return GroupNetwork::mine(t,m);}
};

// Each O mining event uses a new miner with no earlier private block.
// Two stored views here do not represent a physical two-miner network.
struct RandomFresh:RandomGroup {
    using RandomGroup::RandomGroup;
    int mine(long double time,int owner,double u){
        if(owner>0)own[owner]=0;
        return RandomGroup::mine(time,owner,u);
    }
};

void test_random(){
    int checked=0;
    for(bool pref:{false,true})for(int n:{2,10,100})
      for(auto d:std::vector<std::array<long double,3>>{{0,0,0},{1,1,1},{.1L,1,10},{10,.1L,1},{1,10,.1L}}){
        RandomGroup fast(n,d[0],d[1],d[2],pref);RandomHeap ref(n,d[0],d[1],d[2],pref);
        std::mt19937_64 rng(3721),coins(4111);std::exponential_distribution<double> gap(1);
        std::uniform_real_distribution<double> u(0,1);long double time=0;
        for(int i=0;i<3000;++i){time+=gap(rng);double label=u(rng);
            int owner=label<.3?0:1+std::min(n-2,(int)((label-.3)/.7*(n-1)));double coin=u(coins);
            fast.mine(time,owner,coin);ref.mine(time,owner,coin);
            assert(fast.blocks.back().parent==ref.blocks.back().parent);
            assert(fast.blocks.back().height==ref.blocks.back().height);++checked;
        }
    }
    RandomGroup random(2,1,1,1,true);FirstSeenAdapter first(2,1,1,1);
    std::mt19937_64 rng(391);std::exponential_distribution<double> gap(1);
    std::uniform_real_distribution<double> u(0,1);long double time=0;
    for(int i=0;i<10000;++i){time+=gap(rng);int m=u(rng)<.3?0:1;double coin=u(rng);
        random.mine(time,m,coin);first.mine(time,m,coin);
        assert(random.blocks.back().parent==first.blocks.back().parent);
    }
    // O1 starts height 1, A creates a competing block, then O2 randomly
    // extends A. No A event is needed in the next delay window for A to win.
    RandomGroup witness(3,1,1,1,true);witness.mine(0,1,.5);
    witness.mine(.25L,0,.5);witness.mine(1.3L,2,.75);witness.flush(2.4L);
    assert(witness.blocks[3].parent==2 && witness.blocks[2].owner==0);
    assert(witness.A.height==2 && witness.O.height==2);
    RandomFresh fresh(2,1,1,1,true);RandomGroup unique(5002,1,1,1,true);
    int next_owner=1;time=0;
    for(int i=0;i<5000;++i){time+=gap(rng);bool is_A=u(rng)<.3;double coin=u(rng);
        fresh.mine(time,is_A?0:1,coin);unique.mine(time,is_A?0:next_owner++,coin);
        assert(fresh.blocks.back().parent==unique.blocks.back().parent);
    }
    std::cout<<"{\"status\":\"passed\",\"random_heap_parent_checks\":"<<checked
             <<",\"two_miner_random_own_vs_first_seen_checks\":10000,\"fresh_vs_unique_owner_checks\":5000,\"old_comparison_failure_witness\":true}\n";
}

template<class World>void simulate_ties(World& world,int n,double alpha,long double out,
    long double in,long double small,long double end,long double burn,long double lookahead,
    unsigned long long seed,const std::string& rule){
    std::mt19937_64 mining(seed),tie_rng(seed^0xcafef00d591ULL);
    std::exponential_distribution<double> gap(1);std::uniform_real_distribution<double> uniform(0,1);
    long double time=0;std::vector<int> snapshot,confirmation;bool snapped=false,confirmed=false;
    long long generated_A=0,mining_events=0;
    for(;;){
        time+=gap(mining);
        if(!snapped && time>end){world.flush(end);snapshot=world.path(world.peak);snapped=true;}
        if(!confirmed && time>end+lookahead){world.flush(end+lookahead);confirmation=world.path(world.peak);confirmed=true;}
        if(time>end+2*lookahead)break;
        double u=uniform(mining);int owner=u<alpha?0:1+std::min(n-2,(int)((u-alpha)/(1-alpha)*(n-1)));
        world.mine(time,owner,uniform(tie_rng));++mining_events;
        if(time>burn && time<=end && owner==0)++generated_A;
    }
    auto selected=world.path(world.peak);std::vector<bool> after(world.blocks.size(),false);
    long long accepted_A=0,accepted_all=0,changed=0,confirmed_changed=0;
    for(int id:selected){after[id]=true;const auto& b=world.blocks[id];
        if(b.time>burn && b.time<=end){++accepted_all;accepted_A+=b.owner==0;}}
    for(int id:snapshot){const auto& b=world.blocks[id];if(b.time>burn && b.time<=end && !after[id])++changed;}
    for(int id:confirmation){const auto& b=world.blocks[id];if(b.time>burn && b.time<=end && !after[id])++confirmed_changed;}
    double duration=end-burn;
    std::cout<<std::setprecision(17)<<"{\"rule\":\""<<rule<<"\",\"nodes\":"<<n<<",\"alpha\":"<<alpha
        <<",\"out_delay\":"<<(double)out<<",\"in_delay\":"<<(double)in<<",\"small_delay\":"<<(double)small
        <<",\"end_time\":"<<(double)end<<",\"burn_time\":"<<(double)burn<<",\"lookahead_time\":"<<(double)lookahead
        <<",\"seed\":"<<seed<<",\"mining_events\":"<<mining_events<<",\"generated_A\":"<<generated_A
        <<",\"accepted_A\":"<<accepted_A<<",\"accepted_all\":"<<accepted_all<<",\"reward_share_A\":";
    json_ratio(accepted_A,accepted_all);std::cout<<",\"reward_rate_A\":"<<accepted_A/duration
        <<",\"acceptance_A\":";json_ratio(accepted_A,generated_A);
    std::cout<<",\"chain_rate\":"<<accepted_all/duration<<",\"snapshot_blocks_reorganized\":"<<changed
        <<",\"confirmation_blocks_reorganized\":"<<confirmed_changed<<"}\n";
}

int main(int argc,char**argv){
    try{
        if(argc==2 && std::string(argv[1])=="test"){test_random();return 0;}
        if(argc!=11)throw std::runtime_error("rule nodes alpha out in small end burn lookahead seed");
        std::string rule=argv[1];int n=std::stoi(argv[2]);double a=std::stod(argv[3]);
        long double out=std::stold(argv[4]),in=std::stold(argv[5]),small=std::stold(argv[6]);
        long double end=std::stold(argv[7]),burn=std::stold(argv[8]),look=std::stold(argv[9]);
        auto seed=std::stoull(argv[10]);
        if(n<2 || !(a>0 && a<1) || !std::isfinite(a)
           || !std::isfinite(out) || !std::isfinite(in) || !std::isfinite(small)
           || !std::isfinite(end) || !std::isfinite(burn) || !std::isfinite(look)
           || !std::isfinite(end+2*look) || std::min({out,in,small,burn})<0 || end<=burn || look<=0)
            throw std::runtime_error("invalid parameters");
        if(rule=="first-seen"){
            FirstSeenAdapter w(n,out,in,small);simulate_ties(w,n,a,out,in,small,end,burn,look,seed,rule);
        }else if(rule=="random-own" || rule=="random-all"){
            RandomGroup w(n,out,in,small,rule=="random-own");simulate_ties(w,n,a,out,in,small,end,burn,look,seed,rule);
        }else if(rule=="fresh-random-own" || rule=="fresh-random-all"){
            if(n!=2)throw std::runtime_error("fresh model uses 2 stored views; pass nodes=2");
            RandomFresh w(n,out,in,small,rule=="fresh-random-own");simulate_ties(w,n,a,out,in,small,end,burn,look,seed,rule);
        }else throw std::runtime_error("unknown tie rule");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
    return 0;
}
