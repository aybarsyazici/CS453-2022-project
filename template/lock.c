#include <sys/errno.h>
#include "lock.h"

bool lock_init(struct lock_t* lock) {
    return pthread_mutex_init(&(lock->mutex), NULL) == 0
        && pthread_cond_init(&(lock->cv), NULL) == 0;
}

void lock_cleanup(struct lock_t* lock) {
    pthread_mutex_destroy(&(lock->mutex));
    pthread_cond_destroy(&(lock->cv));
}

bool lock_acquire(struct lock_t* lock, size_t holder) {
    if(pthread_mutex_trylock(&(lock->mutex)) == 0) {
        lock->holder = holder;
        return true;
    }
    return false;
}

void lock_release(struct lock_t* lock, size_t holder) {
    if(lock->holder == holder) {
        lock->holder = -1;
        pthread_mutex_unlock(&(lock->mutex));
    }
}

void lock_wait(struct lock_t* lock) {
    pthread_cond_wait(&(lock->cv), &(lock->mutex));
}

void lock_wake_up(struct lock_t* lock) {
    pthread_cond_broadcast(&(lock->cv));
}

bool lock_is_locked(struct lock_t* lock) {
    int locked = pthread_mutex_trylock(&(lock->mutex));
    if (locked == 0) { // If try lock returns 0, the lock is not locked, and we have locked it
        pthread_mutex_unlock(&(lock->mutex)); // Release the lock
    }
    return locked == EBUSY;
}

bool lock_acquire_blocking(struct lock_t* lock, size_t holder) {
    if(pthread_mutex_lock(&(lock->mutex)) == 0) {
        lock->holder = holder;
        return true;
    }
    return false;
}