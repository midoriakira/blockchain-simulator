#include <iostream>
#include <fstream>
#include <random>
#include <queue>
#include <cmath>
#include <iomanip>
#include <climits>
using namespace std;


#define MAX_RN 10
#define MAX_N 1000
using namespace std;
typedef unsigned long long ull;
typedef long long ll;

struct block {
    ll height;
    block* prevBlock;
    int minter; 
    ll time;
    ll rand;
};

struct task {
    ll time;
    int flag; // 0 = block, 1 = propagation
    int minter; // 0
    int from; // 1
    int to; // 1
    block* propagatedBlock;
};

ll currentRound;
ll currentTime = 0;
ll delay = 6000; // 6000, 60000, 300000
ll generationTime = 600000;
block* currentBlock[MAX_N];
task* currentMiningTask[MAX_N];
ll hashrate[MAX_N];
ll totalHashrate;
ll numMain[3][MAX_N];
ll endRound = 100000;
ll propagation[MAX_N][MAX_N];
ll mainLength;
int N = 10;// num of node


void chooseMainchain(block* block1, block* block2, int from, int to, int tie);
void mainChain(block* block1, int tie);
void simulation(int tie);
void reset();
ll prop(int i, int j);


std::random_device seed_gen;
std::mt19937_64 engine(10);// 1, 5, 10, 100
std::uniform_real_distribution<double> dist1(0., 1.0);
std::exponential_distribution<double> dist2(1);
std::normal_distribution<double> dist3(0.0, 1.0);

int main(void) {
    cout << "akira" << endl;
 
    hashrate[0] = N - 1;
    for (int i = 1;i < N;i++) {
        hashrate[i] = 1;
    }


    for (int i = 0;i < N;i++) {
        totalHashrate += hashrate[i];
        cout << "hashrate" << i << ": " << hashrate[i] << endl;
    }

    for (int i = 0;i < N;i++) {
        for (int j = 0;j < N;j++) {
            prop(i, j);
        }
    }

    simulation(0);

    cout << "block propagation time: " << delay << endl;


    return 0;
}

void chooseMainchain(block* block1, block* block2, int from, int to, int tie) {
    if (block1->height > block2->height) {
        currentBlock[to] = block1;
    }

    if (block1->height == block2->height){
        if (tie == 1 && block2->minter != to && block1->rand < block2->rand) { // for the random rule
            currentBlock[to] = block1;
        }

        if (tie == 2 && block2->minter != to && block1->time > block2->time) currentBlock[to] = block1; // for the last-generated rule
    }

    return;
}



ll prop(int i, int j) {
    if (i == j) return 0;
    else return delay;
}

void mainChain(block* block1, int tie) {
    if (block1->height != endRound) {
        ll height = block1->height;
        block* curBlock = block1;
        while (curBlock->height > 0 && curBlock->height != height - 100) { // 100 block finalization
            curBlock = curBlock->prevBlock;
            if (curBlock == nullptr) cout << "eeeee" << endl;
        }
        if(curBlock->height > 0) numMain[tie][curBlock->minter]++;
        mainLength = max(mainLength, curBlock->height);
        return;
    } else {
        block* curBlock = block1;
        while (curBlock->height > mainLength) {
            numMain[tie][curBlock->minter]++;
            curBlock = curBlock->prevBlock;
        }
    }
}

void reset() {
    currentRound = 0;
    currentTime = 0;
    mainLength = 0;
    for (int i = 0;i < N;i++) {
        currentBlock[i] = nullptr;
    }

    return;
}

void simulation(int tie) {
    auto compare = [](task* a, task* b) {
      return a->time > b->time;
    };
    std::priority_queue<
      task*,
      std::vector<task*>,
      decltype(compare) 
    > taskQue {compare};

    std:queue<block*> blockQue;

    std::queue<block*> blockStore;
    std::queue<task*> taskStore;

    block* genesisBlock = new block;
    blockQue.push(genesisBlock);
    genesisBlock->prevBlock = nullptr;
    genesisBlock->height = 0;
    genesisBlock->minter = -1;

    for (int i = 0;i < N;i++) {
        currentBlock[i] = genesisBlock;

        task* nextBlockTask = new task;
        nextBlockTask->time = (ll) (dist2(engine) * generationTime * totalHashrate / hashrate[i]);
        nextBlockTask->flag = 0;
        nextBlockTask->minter = i;
        taskQue.push(nextBlockTask);
        currentMiningTask[i] = nextBlockTask;
    }

    while(taskQue.size() > 0 && currentRound < endRound) {
        task* currentTask = taskQue.top();
        taskQue.pop();
        currentTime = currentTask->time;
        if (currentTask->flag == 0) { //block generation
            int minter = currentTask->minter;
            if (currentMiningTask[minter] != currentTask) continue;
            block* newBlock;
            if (blockStore.size() > 0) {
                newBlock = blockStore.front();blockStore.pop();
            } else {
                newBlock = new block;
            }
            newBlock->prevBlock = currentBlock[minter];
            newBlock->height = currentBlock[minter]->height + 1;
            newBlock->minter = minter;
            newBlock->time = currentTime;
            newBlock->rand = dist1(engine) * (LLONG_MAX - 10);
            currentBlock[minter] = newBlock;

            blockQue.push(newBlock);
            if (blockQue.size() > 10000) { // dont need to record 10000 blocks ago
                block* deleteBlock = blockQue.front();blockQue.pop();
                blockStore.push(deleteBlock);
            }

            task* nextBlockTask;
            if (taskStore.size() > 0) {
                nextBlockTask = taskStore.front();taskStore.pop();
            } else {
                nextBlockTask = new task;
            }
            nextBlockTask->time = currentTime + (ll) (dist2(engine) * generationTime * totalHashrate / hashrate[minter]);
            nextBlockTask->flag = 0;
            nextBlockTask->minter = minter;
            taskQue.push(nextBlockTask);
            currentMiningTask[minter] = nextBlockTask;

            for (int i = 0;i < N;i++) { // propagation task
                task* nextPropTask;
                if (taskStore.size() > 0) {
                    nextPropTask = taskStore.front();taskStore.pop();
                } else {
                    nextPropTask = new task;
                }
                nextPropTask->time = currentTime + prop(minter, i);
                nextPropTask->flag = 1;
                nextPropTask->to = i;
                nextPropTask->from = minter;
                nextPropTask->propagatedBlock = newBlock;
                taskQue.push(nextPropTask);
            }

            if(currentRound < newBlock->height) {
                currentRound = newBlock->height;
            } else { // fork
                
            }
            cout << "blockgeneration, current time: "  << currentTime << ", minter"<< newBlock->minter << ", block height: " << newBlock->height << endl;
        } else { // propagation
            cout << "block propagation, current time: " << currentTime << ", from: " << currentTask->from << ", to: " << currentTask->to << ", height: " << currentTask->propagatedBlock->height << endl;
            int to = currentTask->to;
            int from = currentTask->from;
            chooseMainchain(currentTask->propagatedBlock, currentBlock[to], from, to, tie);
        }

        taskStore.push(currentTask);
    }
}
