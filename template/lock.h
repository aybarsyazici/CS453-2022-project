#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

/**
 * @brief A lock that can only be taken exclusively. Contrarily to shared locks,
 * exclusive locks have wait/wake_up capabilities.
 */
typedef struct lock_t {
    atomic_bool mutex;
}lock_t;

/** Initialize the given lock.
 * @param lock Lock to initialize
 * @return Whether the operation is a success
**/
bool lock_init(struct lock_t* lock);

/** Clean up the given lock.
 * @param lock Lock to clean up
**/
void lock_cleanup(struct lock_t* lock);

/**Acquire the given lock non-blocking.
 * @param lock Lock to acquire
 * @param holder The thread that is acquiring the lock
 * @return Whether the operation is a success
**/
bool lock_acquire(struct lock_t* lock);

/**Wait and acquire the lock, blocking.
 * @param lock Lock to acquire
 * @param holder The thread that is acquiring the lock
 * @return Whether the operation is a success
**/
bool lock_acquire_blocking(struct lock_t* lock);
/** Release the given lock.
 * @param lock Lock to release
 * @param holder The thread that is releasing the lock
**/
void lock_release(struct lock_t* lock);

/** Function to atomically check if the given lock is locked.
 * Unlocks the lock before returning.
 * @param lock Lock to check
 * @return Whether the lock is locked
**/
bool lock_is_locked(struct lock_t* lock);


/** This function is used to check if the given lock is inside the given lockArray
 * @param lockArray array of locks
 * @param size size of the lockArray
 * @param lock lock to check
 * @return Whether the lock is inside the lockArray
**/
bool lock_is_locked_byAnotherThread(struct lock_t** lockArray, int size, struct lock_t* lock);