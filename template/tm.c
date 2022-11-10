/**
 * @file   tm.c
 * @author [...]
 *
 * @section LICENSE
 *
 * [...]
 *
 * @section DESCRIPTION
 *
 * Implementation of your own transaction manager.
 * You can completely rewrite this file (and create more files) as you wish.
 * Only the interface (i.e. exported symbols and semantic) must be preserved.
**/

// Requested features
#define _GNU_SOURCE
#define _POSIX_C_SOURCE   200809L

#ifdef __STDC_NO_ATOMICS__
    #error Current C11 compiler does not support atomic operations
#endif

// External headers

// Internal headers
#include <tm.h>
#include <stdlib.h>
#include <string.h>
#include <printf.h>

#include "macros.h"
#include "tsm_types.h"
#include "tm_helpers.h"

/** Create (i.e. allocate + init) a new shared memory region, with one first non-free-able allocated segment of the requested size and alignment.
 * @param size  Size of the first shared segment of memory to allocate (in bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the shared memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
**/
shared_t tm_create(size_t size, size_t align) {
    // First check if the size is a positive multiple of the alignment
    if (size % align != 0 || size <= 0) {
        return invalid_shared;
    }
    // Now check if the alignment is a power of 2 and if it's larger than sizeof(void *)
    if ((align & (align - 1)) != 0 || align < sizeof(void *)) {
        return invalid_shared;
    }
    Region* region = (Region *) malloc(sizeof(Region));
    if (unlikely(!region)) {
        return invalid_shared;
    }
    // Create a segment node for the first segment of memory of given size.
    segment_node* firstSegment = (segment_node *) malloc(sizeof(segment_node));
    if (unlikely(!firstSegment)) {
        free(region);
        return invalid_shared;
    }
    firstSegment->prev = NULL;
    firstSegment->next = NULL;
    // We allocate the shared memory buffer such that its words are correctly aligned.
    if (posix_memalign(&(firstSegment->freeSpace), align, size) != 0) {
        free(firstSegment);
        free(region);
        return invalid_shared;
    }
    // Initialize the region with 0s
    memset(firstSegment->freeSpace, 0, size);
    // Initialize locks in the segment, as each lock is associated with TSM_WORDS_PER_LOCK words, we need to initialize
    // size / align / TSM_WORDS_PER_LOCK locks.
    // But as this can give us a decimal number, we need to round it up to the next integer.
    // Imagine we have 12 words in the segment(so size/alignment gives 12)
    // And TSM_WORDS_PER_LOCK is 5, then we need to initialize 12/5 = 2.4 locks, but we need to round it up to 3.
    // That why we have +1 in the formula.
    size_t wordCount = size / align;
    firstSegment->lock_size = (wordCount / TSM_WORDS_PER_LOCK) + (wordCount % TSM_WORDS_PER_LOCK == 0 ? 0 : 1);
    firstSegment->locks = (lock_node *) malloc(sizeof(lock_node) * firstSegment->lock_size);
    if(unlikely(!firstSegment->locks)) {
        free(firstSegment->freeSpace);
        free(firstSegment);
        free(region);
        return invalid_shared;
    }
    for (int i = 0; i < firstSegment->lock_size; i++) {
        lock_init(&((firstSegment->locks + i)->lock));
        (firstSegment->locks + i)->version = 0;
    }
    firstSegment->size  = size;
    firstSegment->id    = 0;
    firstSegment->align = align;
    region->allocHead      = firstSegment; // The region starts with one non-deletable segment.
    region->allocTail      = firstSegment; // The region starts with one non-deletable segment.
    region->align       = align;
    region->start       = firstSegment->freeSpace;
    region->globalVersion = 0;
    region->latestTransactionId = 1;
    region->globalLock = ((lock_t*) malloc(sizeof(lock_t)));
    if(unlikely(!region->globalLock)) {
        free(firstSegment->locks);
        free(firstSegment->freeSpace);
        free(firstSegment);
        free(region);
        return invalid_shared;
    }
    lock_init(region->globalLock);
    return region;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
**/
void tm_destroy(shared_t shared) {
    Region* region = (Region *) shared;
    // Free all the segments of the region.
    segment_node* currentSegment = region->allocHead;
    while (currentSegment != NULL) {
        segment_node* nextSegment = currentSegment->next;
        free(currentSegment->freeSpace);
        // Destroy all locks of this segment.
        for (int i = 0; i < currentSegment->lock_size; i++) {
            lock_cleanup(&((currentSegment->locks + i)->lock));
        }
        free(currentSegment->locks);
        free(currentSegment);
        currentSegment = nextSegment;
    }
    lock_cleanup(region->globalLock);
    free(region->globalLock);
    free(region);
}

/** [thread-safe] Return the start address of the first allocated segment in the shared memory region.
 * @param shared Shared memory region to query
 * @return Start address of the first allocated segment
**/
void* tm_start(shared_t shared) {
    return ((Region*)shared)->start;
}

/** [thread-safe] Return the size (in bytes) of the first allocated segment of the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
**/
size_t tm_size(shared_t shared) {
    return ((Region*)shared)->allocHead->size;
}

/** [thread-safe] Return the alignment (in bytes) of the memory accesses on the given shared memory region.
 * @param shared Shared memory region to query
 * @return Alignment used globally
**/
size_t tm_align(shared_t shared) {
    return ((Region *) shared)->align;
}

/** [thread-safe] Begin a new transaction on the given shared memory region.
 * @param shared Shared memory region to start a transaction on
 * @param is_ro  Whether the transaction is read-only
 * @return Opaque transaction ID, 'invalid_tx' on failure
**/
tx_t tm_begin(shared_t shared, bool is_ro) {
    Region* region = (Region *) shared;
    // Create a new transaction.
    transaction * newTransaction = (transaction *) malloc(sizeof(transaction));
    if (unlikely(!newTransaction)) {
        return invalid_tx;
    }
    // Initialize the transaction with an empty readSet and writeSet.
    newTransaction->readSetHead = NULL;
    newTransaction->readSetTail = NULL;
    newTransaction->writeSetHead = NULL;
    newTransaction->writeSetTail = NULL;
    newTransaction->version = region->globalVersion; // The transaction starts with the current global version.
    newTransaction->isReadOnly = is_ro;
    // Fetch and increment the latest transaction id
    newTransaction->id = ++region->latestTransactionId;
    newTransaction->writeSetBloom = (bloom*)malloc(sizeof(bloom));
    bloom_init2(newTransaction->writeSetBloom, 100000, 0.01);
    //newTransaction->readSetMap = cfuhash_new();
    return (tx_t)newTransaction;
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
**/
bool tm_end(shared_t shared, tx_t tx) {
    Region* region = (Region *) shared;
    transaction* transaction = getTransaction(region, tx);
    if(!transaction->isReadOnly){
        // Apply Transactional Locking 2 checks
        // Acquire locks on all the addresses in the writeSet.
        lock_t** locksToAcquire = getLocks(transaction);
        // Sample the global version
        if(acquireLocks(locksToAcquire, transaction->writeSetSize, transaction->id)) {
            // increment and fetch the global version.
            atomic_uint newVersion = ++region->globalVersion;
            if(transaction->version + 1 != newVersion) {
                read_set_node * currentReadSetNode = transaction->readSetHead;
                while (currentReadSetNode != NULL) {
                    lock_node * lockNode = getLockNode(currentReadSetNode->segment, currentReadSetNode->offset);
                    if ( lockNode->version > transaction->version) {
                        // Abort the transaction.
                        releaseLocks(locksToAcquire, transaction->writeSetSize, transaction->id);
                        clearSets(transaction);
                        free(locksToAcquire);
                        return false;
                    }
                    // Check if current read set node is locked
                    if(lock_is_locked_byAnotherThread(&lockNode->lock, transaction->id)) {
                        // Abort the transaction.
                        releaseLocks(locksToAcquire, transaction->writeSetSize, transaction->id);
                        clearSets(transaction);
                        free(locksToAcquire);
                        return false;
                    }
                    currentReadSetNode = currentReadSetNode->next;
                }
            }
            write_set_node* currentWriteSetNode = transaction->writeSetHead;
            // Iterate over the write set and write the new values to shared memory and update their version
            while (currentWriteSetNode != NULL) {
                // Update the version of the address.
                getLockNode(currentWriteSetNode->segment, currentWriteSetNode->offset)->version = newVersion;
                // Write the new value to shared memory.
                memcpy(currentWriteSetNode->segment->freeSpace + currentWriteSetNode->offset * region->align,
                       currentWriteSetNode->value,
                       region->align);
                currentWriteSetNode = currentWriteSetNode->next;
            }
            // Release all the locks.
            releaseLocks(locksToAcquire, transaction->writeSetSize, transaction->id);
            free(locksToAcquire);
        }
        else{
            // Abort the transaction.
            free(locksToAcquire);
            clearSets(transaction);
            return false;
        }
    }
    clearSets(transaction);
    return true;
}

/** [thread-safe] Read operation in the given transaction, source in the shared region and target in a private region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in the shared region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the alignment
 * @param target Target start address (in a private region)
 * @return Whether the whole transaction can continue
**/
bool tm_read(shared_t shared, tx_t tx, void const* source, size_t size, void* target) {
    Region* region = (Region *) shared;
    transaction* transaction = getTransaction(region, tx);
    segment_node* segment = findSegment(region, (void*)source);
    if(segment == NULL){
        clearSets(transaction);
        return false;
    }
    // Note that we are required to read 'size' bytes from the memory location 'source'
    // Because we can read one and write one word at a time, we need to read (size/align) amount of words.
    size_t currentWord = 0;
    for(;currentWord < size / region->align;currentWord++){
        void* currentTarget = (void*) target + currentWord * region->align;
        void* currentSource = (void*) source + currentWord * region->align;

        write_set_node* writeSetNode = NULL;

        void* value = aligned_alloc(region->align, region->align);
        if(value == NULL){
            clearSets(transaction);
            return false;
        }

        size_t offset = (size_t) ((void*) currentSource - segment->freeSpace) / region->align;
        lock_node* lockNode = getLockNode(segment, offset);
        // Sample the version of the lock node.
        unsigned int version = lockNode->version;


        // Check if the transaction is read-only.
        if (!transaction->isReadOnly) {
            // We first check if the memory location we are trying to read from already exists in the write set.
            // As it might be the case that the transaction is trying to read from a memory location that it has already written to.
            writeSetNode = checkWriteSet(region, transaction, currentSource);
            if (writeSetNode != NULL) {
                // If yes, then we can just copy the value from the write set as this is the value we have previously written.
                // The 'new value' field in the node is the value/word this transaction has previously written to this memory location.
                memcpy(value, writeSetNode->value, region->align);
            }
        }
        if(writeSetNode == NULL){
            memcpy(value, currentSource, region->align); // The value was not in write set, so we copy it from the shared memory.
        }
        if(version != lockNode->version){
            clearSets(transaction);
            free(value);
            return false;
        }
        if (lock_is_locked(&lockNode->lock)) {
            clearSets(transaction);
            free(value);
            return false;
        }
        if (lockNode->version > transaction->version) {
            clearSets(transaction);
            free(value);
            return false;
        }
        // Then we need to add the read set node to the read set.
        addReadSet(transaction, segment, offset);
        // Finally, we copy the value/word we have read to the target memory location.
        memcpy(currentTarget, value, region->align);
        free(value);
    }
    return true;
}

/** [thread-safe] Write operation in the given transaction, source in a private region and target in the shared region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in a private region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the alignment
 * @param target Target start address (in the shared region)
 * @return Whether the whole transaction can continue
**/
bool tm_write(shared_t shared, tx_t tx, void const* source, size_t size, void* target) {
    Region* region = (Region *) shared;
    transaction* transaction = getTransaction(region, tx);
    // Now, we need to find which segment the memory location we are trying to write to is in.
    segment_node* segment = findSegment(region, target);
    if(segment == NULL){
        // If no segment is found, then the memory location we are trying to write to is not in the shared memory.
        clearSets(transaction);
        return false;
    }
    // Note that we are required to write 'size' bytes from the source to the target.
    // As we are writing one word at a time, we need to divide the size by the alignment to find out how many words we are going to write.
    size_t currentWord = 0;
    for(;currentWord < size / region->align; currentWord++){
        void* currentTarget = (void*) target + currentWord * region->align; // We start from the target, which is the memory location
        // of the first word we want to write. At each iteration we move to the next word by adding the alignment to the current target.
        void* currentSource = (void*) source + currentWord * region->align;
        // Check if the memory location we are trying to write to already exists in the write set.
        write_set_node* writeSetNode = checkWriteSet(region, transaction, currentTarget);
        if(writeSetNode != NULL){
            // If yes, then we can just copy the new value to the write set.
            // The 'newValue' field in the write set node corresponds to the new value/word that this transaction wants to write.
            // When the same transactions commits, the value in the write set will be written to the shared memory.
            // If the transaction tries to read from the same memory location before it commits, we will return the value from the write set
            // Check the tm_read function for more details on how we do that.
            memcpy(writeSetNode->value, currentSource, region->align);
            continue; // No need to check for the rest of the conditions for this word.
        }
        // If no, then we need to write from the shared memory, as it is the first time we are trying to write to this memory location.
        // First we need to find the offset, i.e. how many words down we are from the current segment.
        size_t offset = (size_t) ((void*) currentTarget - segment->freeSpace) / region->align;
        void* value = aligned_alloc(region->align, region->align);
        if(value == NULL){
            // If the malloc fails, then we return false.
            clearSets(transaction);
            return false;
        }
        memcpy(value, currentSource, region->align);
        // Then we need to add the write set node to the transaction.
        addWriteSet(transaction, segment, offset, value, (int)region->align);
    }
    return true;
}

/** [thread-safe] Memory allocation in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param size   Allocation requested size (in bytes), must be a positive multiple of the alignment
 * @param target Pointer in private memory receiving the address of the first byte of the newly allocated, aligned segment
 * @return Whether the whole transaction can continue (success/nomem), or not (abort_alloc)
**/
alloc_t tm_alloc(shared_t shared, tx_t unused(tx), size_t size, void** target) {
    Region* region = (Region *) shared;
    // Check if size is a positive multiple of the alignment.
    if (size <= 0 || size % region->align != 0) {
        return nomem_alloc;
    }
    // First create a new segment node
    segment_node* newSegment = (segment_node *) malloc(sizeof(segment_node));
    if (unlikely(!newSegment)) {
        return nomem_alloc;
    }
    // Allocate memory for the segment with given alignment and size
    if (posix_memalign(newSegment->freeSpace, region->align, size) != 0) {
        free(newSegment);
        return nomem_alloc;
    }
    memset(newSegment->freeSpace, 0, size); // Set all bytes to 0
    // Initialize locks in the segment, as each lock is associated with TSM_WORDS_PER_LOCK words, we need to initialize
    // size / align / TSM_WORDS_PER_LOCK locks.
    // But as this can give us a decimal number, we need to round it up to the next integer.
    // Imagine we have 12 words in the segment(so size/alignment gives 12)
    // And TSM_WORDS_PER_LOCK is 5, then we need to initialize 12/5 = 2.4 locks, but we need to round it up to 3.
    // That why we have +1 in the formula.
    size_t wordCount = size / region->align;
    newSegment->lock_size = (wordCount / TSM_WORDS_PER_LOCK) + (wordCount % TSM_WORDS_PER_LOCK == 0 ? 0 : 1);
    newSegment->locks = (lock_node *) malloc(sizeof(lock_node) * newSegment->lock_size);
    if(unlikely(!newSegment->locks)) {
        free(newSegment->freeSpace);
        free(newSegment);
        return nomem_alloc;
    }
    atomic_uint globalVersion = region->globalVersion;
    for (int i = 0; i < newSegment->lock_size; i++) {
        lock_init(&((newSegment->locks + i)->lock));
        newSegment->locks[i].version = globalVersion;
    }
    // Lock the region, so no other transaction can allocate memory at the same time.
    lock_acquire_blocking(region->globalLock,0);
    // Add this segment to the list of segments of the region.
    newSegment->prev = region->allocTail; // The prev pointer of this segment points to the last segment of the region.
    newSegment->next = NULL; // The next pointer of this segment is NULL. As it is currently the latest segment of the region.
    newSegment->size  = size;
    newSegment->id    = region->allocTail->id + 1;
    region->allocTail->next = newSegment; // The next pointer of the last segment of the region points to this segment.
    region->allocTail = newSegment; // Latest segment of the region is updated to be this segment.
    // Unlock the region.
    lock_release(region->globalLock,0);
    *target = newSegment->freeSpace;
    return success_alloc;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment to deallocate
 * @return Whether the whole transaction can continue
**/
bool tm_free(shared_t shared, tx_t tx, void* target) {
    // First find the segment node corresponding to the given address.
    Region* region = (Region *) shared;
    segment_node* currentSegment = findSegment(region, target);
    transaction* transaction = getTransaction(region, tx);
    if (currentSegment == NULL) {
        clearSets(transaction);
        return false;
    }
    // If the segment's id is 0 then it is the first segment allocated thus we cannot free it.
    if (currentSegment->id == 0) {
        clearSets(transaction);
        return false;
    }
    // Lock the region, so no other transaction can allocate memory at the same time.
    lock_acquire_blocking(region->globalLock,0);
    // Remove the segment node from the list of segments of the region.
    if (currentSegment->prev != NULL) {
        currentSegment->prev->next = currentSegment->next;
    }
    if (currentSegment->next != NULL) {
        currentSegment->next->prev = currentSegment->prev;
    }
    lock_release(region->globalLock,0);
    // Free the memory buffer.
    free(currentSegment->freeSpace);
    // Cleanup all locks
    for (int i = 0; i < currentSegment->size / region->align / TSM_WORDS_PER_LOCK; i++) {
        lock_cleanup(&((currentSegment->locks + i)->lock));
    }
    free(currentSegment->locks);
    free(currentSegment);
    return true;
}
