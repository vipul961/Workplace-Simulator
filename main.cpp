//HW3_Vipul_Sawant
//347120278

#pragma warning(disable: 4996)

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <atomic>
#include <fstream>


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

mutex m1, m2,m3;
system_clock::duration elapsed_time(microseconds(0));
condition_variable cv_buff_part, cv_buff_product;
atomic<int> c_wait{ 0 }, p_wait{ 0 };//p_completed{ 0 }, c_completed{ 0 };
const int MaxTimePart{30000}, MaxTimeProduct{28000};
int maxIteration = 5;
vector<int> maxBufferState = {7, 6, 5, 5, 4};
vector<int> currentBufferState(5, 0);
vector<int> manufactureWait = {500, 500, 600, 600, 700};
vector<int> bufferWait = {200, 200, 300, 300, 400};
vector<int> assemblyWait = {600, 600, 700, 700, 800};

atomic<int> totalProducts;
system_clock ::time_point startSimulation;

vector<int> ProduceLoadOrder(vector<int> unloadedOrders);

vector<int> ProducePickupOrder(vector<int> localState);

bool CheckBufferStatePart(vector<int> &currentBufferState, vector<int> &loadOrder);

void LoadBufferState(int id, vector<int> &loadOrder, int &iteration);

bool CheckBufferStateProduct(vector<int> &currentBufferState, vector<int> &pickUpOrder);

void UnloadBufferState(int id, vector<int> &pickUpOrder, int &iteration, vector<int> &cartState, vector<int> &localState);

void ProductWorker(int id);

void PartWorker(int id);

void AssembleParts(vector<int>& cartState, vector<int>&localState);

ostream& operator<<(ostream& os, const vector<int>& vec);
ofstream Out("logfile.txt");

int main() {


    const int m = 20, n = 16; //m: number of Part Workers
    //n: number of Product Workers
    //Different numbers might be used during grading.
    vector<thread> PartW, ProductW;
    {
        Timer TT;
        startSimulation = system_clock ::now();
        for (int i = 0; i < m; ++i) {
            PartW.emplace_back(PartWorker, i + 1);
        }
        for (int i = 0; i < n; ++i) {
            ProductW.emplace_back(ProductWorker, i + 1);
        }
        for (auto &i: PartW) i.join();
        for (auto &i: ProductW) i.join();
    }
    Out<<"Final value of total products assembled is: "<<totalProducts<<endl;
    Out << "Finish!" << endl;
    Out.close();
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

    //Out<<endl;
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
        int randNumParts = 1 + rand() % (remainingParts);
        int partType = rand() % 5;
        if ((partNum.size() <= 3 && partNum.find(partType) != partNum.end()) || partNum.size() < 3 ) {
            if ((partNum.size() == 3 || partNum.size() == 2) && partNum.find(partType) != partNum.end()) {
                pickUpOrder[partType] += randNumParts;
                remainingParts -= randNumParts;
                this_thread::sleep_for(microseconds(manufactureWait[partType]) * randNumParts);
            } else if (partNum.size() == 2 && partNum.find(partType) == partNum.end()) {
                partNum[partType]++;
                pickUpOrder[partType] += randNumParts;
                remainingParts -= randNumParts;
                this_thread::sleep_for(microseconds(manufactureWait[partType]) * randNumParts);
            } else if (pickUpOrder[partType] + randNumParts != fixedOrderItemLimit) {
                partNum[partType]++;
                pickUpOrder[partType] += randNumParts;
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

void LoadBufferState(int id, vector<int> &loadOrder, int &iterationPart) {
    loadOrder = ProduceLoadOrder(loadOrder);
    unique_lock UL1(m1);
    auto start = system_clock::now();
    system_clock::duration  accumulatedTime = microseconds (0);
    auto deadline = system_clock ::now()+ microseconds(MaxTimePart);
    while (checkOrder(loadOrder)) {
        auto t1 = system_clock::now();
        if (cv_buff_part.wait_until(UL1, deadline, [&loadOrder]() {
            return CheckBufferStatePart(currentBufferState, loadOrder);
        })) {
            auto t2 = system_clock ::now();
            accumulatedTime += (t2-t1);
            {
                Out << "Current Time: "<< duration_cast<microseconds>(t2-startSimulation).count() <<" us"<< endl;
                Out << "Iteration: " << iterationPart + 1 << endl;
                Out << "Part worker ID: " << id << endl;
                if (duration_cast<microseconds>(accumulatedTime) <= microseconds(1)) {
                    Out << "Status: New Load Order" << endl;
                } else {
                    Out << "Status: Wakeup-Notified" << endl;
                }
                Out << "Accumulated Wait Time: " << duration_cast<microseconds>(accumulatedTime).count() << " us" << endl;
                Out << "Buffer State: ";
                Out<<currentBufferState<<endl;
                Out << "Load Order: ";
                Out<<loadOrder<<endl;
            }
            for (int i = 0; i < currentBufferState.size(); i++) {
                int remainingSpace = maxBufferState[i] - currentBufferState[i];
                if (remainingSpace > 0 && loadOrder[i] > 0) {
                    int amount_to_load = min(remainingSpace, loadOrder[i]);
                    this_thread::sleep_for(microseconds(bufferWait[i]) * amount_to_load);
                    currentBufferState[i] += amount_to_load;
                    loadOrder[i] -= amount_to_load;
                }
            }
            {
                Out << "Updated Buffer State: ";
                Out<<currentBufferState<<endl;
                Out << "Updated Load Order: ";
                Out<<loadOrder<<endl;
                Out << endl;
            }
            cv_buff_product.notify_all();
            cv_buff_part.notify_all();
            auto t3 = system_clock ::now();
            accumulatedTime += (t3 - t2);
        } else {
            auto deadlineTime = deadline - system_clock::now();
            {
                lock_guard LG1(m2);
                Out << "Current Time: "<< duration_cast<microseconds>(system_clock::now() - startSimulation).count() <<" us"<< endl;
                Out << "Iteration: " << iterationPart + 1 << endl;
                Out << "Part worker ID: " << id << endl;
                Out << "Status: Wakeup-Timeout" << endl;
                Out << "Accumulated Wait Time: " << duration_cast<microseconds>((system_clock::now() - start)).count() + duration_cast<microseconds>(deadlineTime).count() << " us" << endl;
                Out << "Buffer State: ";
                Out <<currentBufferState<<endl;
                Out << "Load Order: ";
                Out <<loadOrder<<endl;
                Out << "Updated Buffer State: ";
                Out <<currentBufferState<<endl;
                Out << "Updated Load Order: ";
                Out <<loadOrder<<endl;
                Out <<endl;
            }
            cv_buff_part.notify_all();
            cv_buff_product.notify_all();
            break;
        }
    }
    cv_buff_part.notify_all();
    cv_buff_product.notify_all();
    iterationPart++;
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
    bool timeoutProduct = false;
    unique_lock UL1(m1);
    auto start = system_clock ::now();
    system_clock::duration accumulatedTime = microseconds (0);
    auto deadline = system_clock ::now() + microseconds(MaxTimeProduct);
    while (checkOrder(pickUpOrder)) {
        auto t1 = system_clock ::now();
        if (cv_buff_part.wait_until(UL1, deadline, [&pickUpOrder]() {
            return CheckBufferStateProduct(currentBufferState, pickUpOrder);
        })) {
            auto t2 = system_clock ::now();
            accumulatedTime+= t2-t1;
            {
                Out << "Current Time: "<< duration_cast<microseconds>(t2-startSimulation).count() <<" us"<< endl;
                Out << "Iteration: " << iteration + 1 << endl;
                Out << "Product worker ID: " << id << endl;
                if (duration_cast<microseconds>(t2 - t1) <= microseconds(1)) {
                    Out << "Status: New Pick Order" << endl;
                } else {
                    Out << "Status: Wakeup-Notified" << endl;
                }
                Out << "Accumulated Wait Time: " << duration_cast<microseconds>(accumulatedTime).count() << " us" << endl;
                Out << "Buffer State: "<<currentBufferState<<endl;
                Out << "Pickup Order: "<<pickUpOrder<<endl;
                Out << "Local State: "<<localState<<endl;
                Out << "Cart State: "<<cartState<<endl;
            }
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
            {
                Out << "Updated Buffer State: "<<currentBufferState<<endl;
                Out << "Updated Pickup Order: "<<pickUpOrder<<endl;
                Out << "Updated Local State: "<<localState<<endl;
                Out << "Updated Cart State: "<<cartState<<endl;
                Out<<endl;
            }
            cv_buff_part.notify_all();
            cv_buff_product.notify_all();
            auto t3 = system_clock ::now();
            accumulatedTime += ( t3 - t2 );
        } else {
            timeoutProduct=true;
            auto deadlineTime = deadline - system_clock::now();
            {
                lock_guard LG1(m2);
                Out << "Current Time: " << duration_cast<microseconds>(system_clock::now() - startSimulation).count() << " us" << endl;
                Out << "Iteration: " << iteration + 1 << endl;
                Out << "Product worker ID: " << id << endl;
                Out << "Status: Wakeup-Timeout" << endl;
                Out << "Accumulated Wait Time: " << duration_cast<microseconds>(system_clock::now() - start ).count()+duration_cast<microseconds>(deadlineTime).count() << " us" << endl;
                Out << "Buffer State: "<<currentBufferState<<endl;
                Out << "Pickup Order: "<<pickUpOrder<<endl;
                Out << "Local State: "<<localState<<endl;
                Out << "Cart State: "<<cartState<<endl;
                Out << "Updated Buffer State: "<<currentBufferState<<endl;
                Out << "Updated Pickup Order: "<<pickUpOrder<<endl;
                for(int i=0;i<cartState.size();i++){
                    localState[i]+=cartState[i];
                    cartState[i]=0;
                }
                Out << "Updated Local State: "<<localState<<endl;
                Out << "Updated Cart State: "<<cartState<<endl;
            }
            cv_buff_part.notify_all();
            cv_buff_product.notify_all();
            break;
        }
    }
    cv_buff_part.notify_all();
    cv_buff_product.notify_all();
    iteration++;
    if(!timeoutProduct){
        AssembleParts(cartState,localState);
        totalProducts++;
    }
    {
        lock_guard LG1(m2);
        Out << "Current Time: "<< duration_cast<microseconds>(system_clock::now()- startSimulation).count()<<" us"<< endl;
        Out << "Updated Local State: "<<localState<<endl;
        Out << "Updated Cart State: "<<cartState<<endl;
        Out<<"Total Completed Products: "<<totalProducts<<endl;
        Out<<endl;
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

    vector<int> pickUpOrder(5,0),cartState(5,0),localState(5,0);
    int iteration = 0;
    while(iteration<maxIteration){
        UnloadBufferState(id, pickUpOrder, iteration,cartState,localState);
    }

}

void PartWorker(int id) {
    int iterationPart=0;
    vector<int> loadOrder(5);
    while(iterationPart<maxIteration){
        LoadBufferState(id, loadOrder, iterationPart);
    }
}

ostream& operator<<(ostream& os, const vector<int>& vec) {
    os << "( ";
    for(int i=0;i<vec.size();i++){
        os<<vec[i];
        if(i<4){
            os<<",";
        }
    }
    os << " )";
    return os;
}

//Test the git push