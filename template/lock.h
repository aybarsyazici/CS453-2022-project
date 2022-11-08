#pragma once

#include <pthread.h>
#include <stdbool.h>

/**
 * @brief A lock that can only be taken exclusively. Contrarily to shared locks,
 * exclusive locks have wait/wake_up capabilities.
 */
typedef struct lock_t {
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    size_t holder;
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
bool lock_acquire(struct lock_t* lock, size_t holder);

/**Wait and acquire the lock, blocking.
 * @param lock Lock to acquire
 * @param holder The thread that is acquiring the lock
 * @return Whether the operation is a success
**/
bool lock_acquire_blocking(struct lock_t* lock, size_t holder);
/** Release the given lock.
 * @param lock Lock to release
 * @param holder The thread that is releasing the lock
**/
void lock_release(struct lock_t* lock, size_t holder);

/** Wait until woken up by a signal on the given lock.
 *  The lock is released until lock_wait completes at which point it is acquired
 *  again. Exclusive lock access is enforced.
 * @param lock Lock to release (until woken up) and wait on.
**/
void lock_wait(struct lock_t* lock);

/** Wake up all threads waiting on the given lock.
 * @param lock Lock on which other threads are waiting.
**/
void lock_wake_up(struct lock_t* lock);

/** Function to atomically check if the given lock is locked.
 * Unlocks the lock before returning.
 * @param lock Lock to check
 * @return Whether the lock is locked
**/
bool lock_is_locked(struct lock_t* lock);
