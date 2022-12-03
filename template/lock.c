#include <unistd.h>
#include <printf.h>
#include "lock.h"

bool lock_init(struct lock_t* lock) {
    lock->mutex = false;
    lock->holder = 0;
    return true;
}

void lock_cleanup(struct lock_t* lock) {
    lock->mutex = false;
    lock->holder = 0;
}

bool lock_acquire(struct lock_t* lock, unsigned long holder) {
    if(atomic_compare_exchange_strong(&lock->mutex, &(bool){false}, true)) {
        lock->holder = holder;
        return true;
    }
    return false;
}

void lock_release(struct lock_t* lock, unsigned long holder) {
    if(lock->holder == holder) {
        lock->holder = 0;
        lock->mutex = false;
    }
    else{
        printf("SHOULDN'T HAPPEN\n");
    }
}

bool lock_is_locked(struct lock_t* lock) {
    return lock->mutex;
}

bool lock_acquire_blocking(struct lock_t* lock, unsigned long holder) {
    int bound = 0;
    while(atomic_compare_exchange_strong(&lock->mutex, &(bool){false}, true)){
        if (bound > 100) return false;
        //usleep(10);
        bound++;
    }
    lock->holder = holder;
    return true;
}

bool lock_is_locked_byAnotherThread(struct lock_t** lockArray, int size, struct lock_t* lock){
    if(lock->mutex){
        for(int i = 0; i < size; i++){
            if(lockArray[i] == lock){
                return false;
            }
        }
        // printf("Lock is locked by another thread\n");
        return true;
    }
    return false;
}

bool lock_is_locked_byAnotherThread_holder(struct lock_t *lock, unsigned long potentialHolder) {
    return lock->mutex && lock->holder != potentialHolder;
}
