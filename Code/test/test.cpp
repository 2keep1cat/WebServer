#include "thread_sync.h"
using namespace std;

void *afun(void *arg){
    wait();
	cout<<"a"<<endl;
	return NULL;
}
void *bfun(void *arg){
	sleep(1);
    signal();
    cout<<"b"<<endl;
    sleep(1);
	return NULL;
}
int main(){
    try {
        sem t;
    } catch (const std::exception &e) {
        std::cerr << "Semaphore initialization failed: " << e.what() << std::endl;
        return 1;
    }
    pthread_t a_id,b_id;
    pthread_create(&a_id, NULL, afun, NULL);
    pthread_create(&b_id, NULL, bfun, NULL);
    pthread_join(a_id, NULL);
    pthread_join(b_id, NULL);
    
    
    return 0;
}