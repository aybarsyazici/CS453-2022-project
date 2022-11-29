#include <unistd.h>
#include "lock.h"

bool lock_init(struct lock_t* lock) {
    lock->mutex = false;
    return true;
}

void lock_cleanup(struct lock_t* lock) {
    lock->mutex = false;
}

bool lock_acquire(struct lock_t* lock) {
    if(atomic_compare_exchange_strong(&lock->mutex, &(bool){false}, true)) {
        return true;
    }
    return false;
}

void lock_release(struct lock_t* lock) {
    lock->mutex = false;
}

bool lock_is_locked(struct lock_t* lock) {
    return lock->mutex;
}

bool lock_acquire_blocking(struct lock_t* lock) {
    int bound = 0;
    while(atomic_compare_exchange_strong(&lock->mutex, &(bool){false}, true)){
        if (bound > 100) return false;
        usleep(10);
        bound++;
    }
    return true;
}

bool lock_is_locked_byAnotherThread(struct lock_t** lockArray, int size, struct lock_t* lock){
    for(int i = 0; i < size; i++){
        if(lockArray[i] == lock){
            return false;
        }
    }
    return lock_is_locked(lock);
}
