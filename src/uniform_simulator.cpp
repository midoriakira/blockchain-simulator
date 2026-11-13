// Independent constant-delay, first-seen simulator. Constant total Poisson rate.
// Uniform broadcasts mature in generation order. Mining uses superposition,
// unlike the reference's per-miner clocks and per-recipient event heap.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <climits>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Record { int parent, height, owner; long double time; };
struct World {
    std::vector<Record> blocks{{0,0,-1,0}};
    std::vector<int> tips, starter{-1};
    size_t next_delivery=1;
    int height=0, peak_tip=0;
    long double delay;
    World(int n, long double delta):tips(n,0),delay(delta){}
    int mine(long double time, int owner) {
        assert(owner>=0 && owner<(int)tips.size());
        assert(blocks.size()==1 || time>=blocks.back().time);
        while(next_delivery<blocks.size() && blocks[next_delivery].time+delay<=time) {
            int delivered=(int)next_delivery++;
            for(int& tip:tips)
                if(blocks[delivered].height>blocks[tip].height) tip=delivered;
        }
        int parent=tips[owner], id=(int)blocks.size();
        blocks.push_back({parent,blocks[parent].height+1,owner,time});
        tips[owner]=id;
        if(blocks.back().height>height) {
            assert(blocks.back().height==height+1);
            ++height; starter.push_back(owner); peak_tip=id;
        }
        return id;
    }
    std::vector<int> path(int tip) const {
        std::vector<int> ids(blocks[tip].height+1);
        while(tip) {
            ids[blocks[tip].height]=tip;
            int parent=blocks[tip].parent;
            assert(blocks[parent].height==blocks[tip].height-1);
            tip=parent;
        }
        return ids;
    }
};

void nullable_ratio(long long a,long long b) {
    if(b) std::cout<<(double)a/b; else std::cout<<"null";
}

void output(const World& w,const std::vector<int>& selected,int first,int last,
            const std::string& fields) {
    long long nA=0,nO=0,wA=0,wO=0,AA=0,OA=0,prevA=0,prevO=0;
    for(int h=first;h<=last;++h) {
        bool startA=w.starter[h]==0, win=w.blocks[selected[h]].owner==0;
        if(startA){++nA;wA+=win;}else{++nO;wO+=win;}
        if(h>first) {
            if(w.starter[h-1]==0){++prevA;AA+=startA;}
            else{++prevO;OA+=startA;}
        }
    }
    assert(nA+nO==last-first+1);
    std::cout<<std::setprecision(17)<<"{"<<fields
             <<",\"first_height\":"<<first<<",\"last_height\":"<<last
             <<",\"A_start_count\":"<<nA<<",\"O_start_count\":"<<nO
             <<",\"A_wins_on_A_start\":"<<wA<<",\"A_wins_on_O_start\":"<<wO
             <<",\"r_A\":"<<(double)(wA+wO)/(nA+nO)
             <<",\"pi_A\":"<<(double)nA/(nA+nO)
             <<",\"pi_O\":"<<(double)nO/(nA+nO)<<",\"W_A\":";
    nullable_ratio(wA,nA); std::cout<<",\"W_O\":";nullable_ratio(wO,nO);
    std::cout<<",\"X_A\":";nullable_ratio(AA,prevA);
    std::cout<<",\"X_O\":";nullable_ratio(OA,prevO);std::cout<<"}\n";
}

void self_test() {
    World w(2,1);
    w.mine(.1L,0);w.mine(.2L,1);w.mine(.3L,0);w.mine(1.25L,1);w.mine(1.35L,1);
    assert(w.blocks[1].parent==0 && w.blocks[2].parent==0);
    assert(w.blocks[3].parent==1 && w.blocks[4].parent==2);
    assert(w.blocks[5].parent==4); // delivered equal-height competitor does not replace own tip
    World z(2,0);z.mine(.1L,0);z.mine(.2L,1);z.mine(.3L,0);
    assert(z.blocks[2].parent==1 && z.blocks[3].parent==2);
    for(int n:{2,1000}) {
        World loss(n,1);
        loss.mine(.1L,0);loss.mine(.2L,1);loss.mine(.3L,0);
        loss.mine(1.25L,1);loss.mine(1.35L,1);loss.mine(2.5L,0);loss.mine(3.6L,0);
        auto selected=loss.path(loss.peak_tip);
        assert(loss.starter[1]==0 && loss.blocks[selected[1]].owner==1);
        for(int tip:loss.tips)assert(loss.blocks[loss.path(tip)[1]].owner==1);
    }
    std::cout<<"{\"deterministic_scenarios\":4,\"WA_finite_network_counterexample\":true,\"status\":\"passed\"}\n";
}

int main(int argc,char**argv) {
    try {
        if(argc==2 && std::string(argv[1])=="test"){self_test();return 0;}
        if(argc==5 && std::string(argv[1])=="replay") {
            std::ifstream file(argv[2]); if(!file)throw std::runtime_error("trace missing");
            int n=std::stoi(argv[3]);long double delay=std::stold(argv[4]);
            if(n<2 || !std::isfinite(delay) || delay<0)throw std::runtime_error("invalid replay parameters");
            World w(n,delay);
            std::string line;std::getline(file,line);
            int checked=0;
            while(std::getline(file,line)) {
                std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);
                int id,owner,parent,height;long double time;
                if(!(in>>id>>time>>owner>>parent>>height)) throw std::runtime_error("invalid trace");
                if(id!=(int)w.blocks.size() || owner<0 || owner>=n || parent<0 || parent>=id
                   || height<1 || !std::isfinite(time) || time<w.blocks.back().time)
                    throw std::runtime_error("invalid trace event");
                int actual=w.mine(time,owner);
                if(actual!=id || w.blocks[actual].parent!=parent || w.blocks[actual].height!=height)
                    throw std::runtime_error("parent/height mismatch at block "+std::to_string(id));
                ++checked;
            }
            if(w.height<1201)throw std::runtime_error("trace must reach height 1201 for the measurement window");
            output(w,w.path(w.peak_tip),1001,w.height-200,
                   "\"mode\":\"replay\",\"checked_blocks\":"+std::to_string(checked));
            return 0;
        }
        if(argc!=9 || std::string(argv[1])!="simulate") {
            std::cerr<<"simulate nodes pnum pden eta height seed lookahead | replay trace nodes delay | test\n";
            return 2;
        }
        int n=std::stoi(argv[2]);double alpha=(double)std::stoi(argv[3])/std::stoi(argv[4]);
        long double eta=std::stold(argv[5]);int horizon=std::stoi(argv[6]);
        auto seed=std::stoull(argv[7]);int lookahead=std::stoi(argv[8]);
        if(n<2 || !(alpha>0 && alpha<1) || !std::isfinite(alpha) || !std::isfinite(eta)
           || eta<0 || horizon<1201 || lookahead<0 || horizon>INT_MAX-lookahead)
            throw std::runtime_error("invalid parameter");
        World w(n,eta);std::mt19937_64 rng(seed);
        std::exponential_distribution<double> gap(1);
        std::uniform_real_distribution<double> choice(0,1);
        std::vector<int> snapshot;long double time=0;
        while(w.height<horizon+lookahead) {
            time+=gap(rng);double u=choice(rng);
            int owner=u<alpha?0:1+std::min(n-2,(int)((u-alpha)/(1-alpha)*(n-1)));
            w.mine(time,owner);
            if(w.height==horizon && snapshot.empty())snapshot=w.path(w.peak_tip);
        }
        auto continued=w.path(w.peak_tip);int changed=0;
        for(int h=1001;h<=horizon-200;++h)changed+=snapshot[h]!=continued[h];
        std::ostringstream fields;
        fields<<std::setprecision(17)<<"\"mode\":\"simulate\",\"nodes\":"<<n
              <<",\"alpha\":"<<alpha<<",\"eta\":"<<(double)eta
              <<",\"height\":"<<horizon<<",\"seed\":"<<seed
              <<",\"lookahead_heights\":"<<lookahead
              <<",\"prefix_blocks_changed_after_lookahead\":"<<changed
              <<",\"generated_blocks\":"<<w.blocks.size()-1;
        output(w,snapshot,1001,horizon-200,fields.str());
    }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
    return 0;
}
