#pragma warning(disable: 4996)

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include <sstream>


using namespace std;
using namespace chrono;
using namespace literals::chrono_literals;//s, h, min, ms, us, ns

class Timer {
public:
    chrono::system_clock::time_point Begin;
    chrono::system_clock::time_point End;
    chrono::system_clock::duration RunTime;

    Timer() {//constructor
        Begin = chrono::system_clock::now();
    }

    ~Timer() {
        End = chrono::system_clock::now();
        RunTime = End - Begin;
        cout << "Run Time is " << chrono::duration_cast<chrono::milliseconds>(RunTime).count() << "ms" << endl;
    }
    // hours, microseconds, milliseconds, minutes, nanoseconds, seconds can be used for units
};

mutex m1, m2, m3;
condition_variable cv_buff_part, cv_buff_product;
const int MaxTimePart{30000}, MaxTimeProduct{28000};
int maxIteration = 5;
vector<int> maxBufferState = {7, 6, 5, 5, 4};
vector<int> currentBufferState(5, 0);
vector<int> manufactureWait = {500, 500, 600, 600, 700};
vector<int> bufferWait = {200, 200, 300, 300, 400};
vector<int> assemblyWait = {600, 600, 700, 700, 800};
atomic_int totalProducts;
steady_clock::time_point startSimulation;

vector<int> ProduceLoadOrder(vector<int> unloadedOrders);

vector<int> ProducePickupOrder(vector<int> localState);

bool CheckBufferStatePart(vector<int> &currentBufferState, vector<int> &loadOrder);

void LoadBufferState(int id, vector<int> &loadOrder, int &iteration);

bool CheckBufferStateProduct(vector<int> &currentBufferState, vector<int> &pickUpOrder);

void UnloadBufferState(int id, vector<int> &pickUpOrder, int &iteration, vector<int> &cartState, vector<int> &localState);

void ProductWorker(int id);

void PartWorker(int id);

void AssembleParts(vector<int>& cartState, vector<int>&localState);

int main() {


    const int m = 1, n = 0; //m: number of Part Workers
    //n: number of Product Workers
    //Different numbers might be used during grading.
    vector<thread> PartW, ProductW;
    {
        Timer TT;
        startSimulation = steady_clock::now();
        for (int i = 0; i < m; ++i) {
            PartW.emplace_back(PartWorker, i + 1);
        }
        for (int i = 0; i < n; ++i) {
            ProductW.emplace_back(ProductWorker, i + 1);
        }
        for (auto &i: PartW) i.join();
        for (auto &i: ProductW) i.join();
    }
    cout<<"Final value of total products assembled is: "<<totalProducts<<endl;
    cout << "Finish!" << endl;



    return 0;
}

vector<int> ProduceLoadOrder(vector<int> unloadedOrders) {

    vector<int> loadOrder(unloadedOrders);
    int fixedOrderItemLimit = 6;
    int presentOrderItems = 0;
    for (auto i: unloadedOrders) {
        presentOrderItems += i;
    }

    int remainingParts = fixedOrderItemLimit - presentOrderItems;
    while (remainingParts != 0) {
        srand(system_clock::now().time_since_epoch().count());
        int randNumParts = rand() % (remainingParts);
        int partType = rand() % 5;
        this_thread::sleep_for(randNumParts * microseconds(manufactureWait[partType]));
        loadOrder[partType] += ++randNumParts;
        remainingParts -= randNumParts;
    }
    return loadOrder;
}

vector<int> ProducePickupOrder(vector<int> localState) {
    vector<int> pickUpOrder(localState);
    int fixedOrderItemLimit = 5;
    int presentOrderItems = 0;
    unordered_map<int, int> partNum;
    for (int i = 0; i < localState.size(); i++) {
        presentOrderItems += localState[i];
        if (localState[i] > 0)partNum[i]++;
    }

    int remainingParts = fixedOrderItemLimit - presentOrderItems;
    while (remainingParts != 0) {
        srand(system_clock::now().time_since_epoch().count());
        int randNumParts = rand() % (remainingParts);
        int partType = rand() % 5;
        if ((partNum.size() <= 3 && partNum.find(partType) != partNum.end()) || partNum.size() < 3 ) {
            if ((partNum.size() == 3 || partNum.size() == 2) && partNum.find(partType) != partNum.end()) {
                pickUpOrder[partType] += ++randNumParts;
                remainingParts -= randNumParts;
                this_thread::sleep_for(microseconds(manufactureWait[partType]) * randNumParts);
            } else if (partNum.size() == 2 && partNum.find(partType) == partNum.end()) {
                partNum[partType]++;
                pickUpOrder[partType] += ++randNumParts;
                remainingParts -= randNumParts;
                this_thread::sleep_for(microseconds(manufactureWait[partType]) * randNumParts);
            } else if (localState[partType] + randNumParts + 1 != fixedOrderItemLimit) {
                pickUpOrder[partType] += ++randNumParts;
                remainingParts -= randNumParts;
                this_thread::sleep_for(microseconds(manufactureWait[partType]) * randNumParts);
            }
        }
    }

    for(int i=0;i<pickUpOrder.size();i++){
        pickUpOrder[i] = pickUpOrder[i]-localState[i];
    }
    return pickUpOrder;
}

bool CheckBufferStatePart(vector<int> &currentBufferState, vector<int> &loadOrder) {
    for (int i = 0; i < currentBufferState.size(); ++i) {
        if (currentBufferState[i] < maxBufferState[i] && loadOrder[i] > 0) {
            return true;
        }
    }
    return false;
}

bool checkOrder(vector<int> &order) {
    for (auto i: order) {
        if (i > 0)return true;
    }
    return false;
}

void LoadBufferState(int id, vector<int> &loadOrder, int &iteration) {
    loadOrder = ProduceLoadOrder(loadOrder);
    auto deadline = system_clock::now() + microseconds(MaxTimePart);
    system_clock::duration elapsed;
    while (checkOrder(loadOrder)) {
        unique_lock UL1(m1);
        auto t1 =system_clock::now();
        if (cv_buff_part.wait_until(UL1, deadline, [&loadOrder]() {
            return CheckBufferStatePart(currentBufferState, loadOrder);
        })) {
            auto t2 = system_clock::now();
            elapsed += duration_cast<microseconds>(t2 - t1);
            cout<<"Wait time: "<< duration_cast<microseconds>(elapsed).count()<<" us"<<endl;
            for (int i = 0; i < currentBufferState.size(); i++) {
                int remainingSpace = maxBufferState[i] - currentBufferState[i];
                if (remainingSpace > 0 && loadOrder[i] > 0) {
                    int amount_to_load = min(remainingSpace, loadOrder[i]);
                    this_thread::sleep_for(microseconds(bufferWait[i]) * amount_to_load);
                    currentBufferState[i] += amount_to_load;
                    loadOrder[i] -= amount_to_load;
                }
            }
            cv_buff_part.notify_all();
            cv_buff_product.notify_all();
            auto t3 = system_clock::now();
            elapsed += duration_cast<microseconds>(t3 - t2);
            //deadline = system_clock::now() + microseconds (MaxTimePart) - elapsed;
            deadline = deadline - elapsed;
            cout<<"new deadline "<<duration_cast<microseconds>(deadline-system_clock::now()).count()<<" us"<<endl;

        } else {
            auto t2 = system_clock::now();
            cout<<"Wait time before timeout: "<< duration_cast<microseconds>(t2-t1-elapsed).count()<<" us"<<endl;


            cv_buff_part.notify_all();
            cv_buff_product.notify_all();
            UL1.unlock();
            if (iteration < maxIteration-1) {
                LoadBufferState(id, loadOrder, ++iteration);
            }
            return;
        }
    }
    cv_buff_part.notify_all();
    cv_buff_product.notify_all();
    if (iteration < maxIteration - 1) {
        //UL1.unlock();
        LoadBufferState(id, loadOrder, ++iteration);
    } else {
        //cout << "Cannot update again: part worker : " << id << endl;
    }
}


bool CheckBufferStateProduct(vector<int> &currentBufferState, vector<int> &pickUpOrder) {
    for (int i = 0; i < currentBufferState.size(); ++i) {
        if (currentBufferState[i] > 0 && pickUpOrder[i] > 0) {
            return true;
        }
    }
    return false;
}

void UnloadBufferState(int id, vector<int> &pickUpOrder, int &iteration,vector<int> &cartState,vector<int> &localState) {

    pickUpOrder = ProducePickupOrder(localState);
    auto deadline = steady_clock::now() + microseconds(MaxTimeProduct);
    while (checkOrder(pickUpOrder)) {
        unique_lock UL1(m1);

        if (cv_buff_part.wait_until(UL1, deadline, [&pickUpOrder]() {
            return CheckBufferStateProduct(currentBufferState, pickUpOrder);
        })) {

            for (int i = 0; i < currentBufferState.size(); i++) {
                int availableSpace = currentBufferState[i];
                if (availableSpace > 0 && pickUpOrder[i] > 0) {
                    int amount_to_unload = min(availableSpace, pickUpOrder[i]);
                    this_thread::sleep_for(microseconds(bufferWait[i]) * amount_to_unload);
                    currentBufferState[i] -= amount_to_unload;
                    pickUpOrder[i] -= amount_to_unload;
                    cartState[i] += amount_to_unload;
                }
            }

            cv_buff_part.notify_all();
            cv_buff_product.notify_all();
        } else {
                for(int i=0;i<cartState.size();i++){
                    localState[i]+=cartState[i];
                    cartState[i]=0;
                }

            cv_buff_part.notify_all();
            cv_buff_product.notify_all();
            if (iteration < maxIteration - 1) {
                UL1.unlock();
                UnloadBufferState(id, pickUpOrder, ++iteration,cartState,localState);
            }
            return;
        }
    }
    AssembleParts(cartState,localState);
    totalProducts++;
    cv_buff_part.notify_all();
    cv_buff_product.notify_all();
    if (iteration < maxIteration - 1) {

        UnloadBufferState(id, pickUpOrder, ++iteration,cartState,localState);
    } else {
        //cout << "Cannot update again: product worker : " << id << endl;
    }
}

void AssembleParts(vector<int> &cartState,vector<int>&localState) {
    for(int i=0;i<cartState.size();i++){
        this_thread::sleep_for(microseconds(assemblyWait[i]) * (cartState[i]+localState[i]));
        cartState[i]=0;
        localState[i]=0;
    }
}


void ProductWorker(int id) {

    vector<int> pickUpOrder(5),cartState(5),localState(5);
    int iteration = 0;
    UnloadBufferState(id, pickUpOrder, iteration,cartState,localState);

}

void PartWorker(int id) {

    vector<int> loadOrder(5);
    int iteration = 0;
    LoadBufferState(id, loadOrder, iteration);

}

//Test the git push