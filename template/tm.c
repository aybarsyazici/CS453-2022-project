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
#include <sys/types.h>
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
    // printf("SET_SIZE: %d, HASH_SIZE: %d, TSM_ARRAY_SIZE: %d, TSM_WORDS_PER_LOCK: %d\n", SET_START_SIZE, HASH_SIZE, TSM_ARRAY_SIZE, TSM_WORDS_PER_LOCK);
    Region* region = (Region *) malloc(sizeof(Region));
    if (unlikely(!region)) {
        return invalid_shared;
    }

    region->segments.elements = (segment_node **) malloc(sizeof(segment_node *) * 65536);
    region->segments.locks = (atomic_bool *) malloc(sizeof(atomic_bool) * 65536);
    if(unlikely(!region->segments.elements || !region->segments.locks)) {
        return invalid_shared;
    }
    for(int i = 0; i < 65536; i++) {
        region->segments.elements[i] = NULL;
        atomic_init(&region->segments.locks[i], false);
    }

    // Create a segment node for the first segment of memory of given size.
    segment_node* firstSegment = (segment_node *) malloc(sizeof(segment_node));
    if (unlikely(!firstSegment)) {
        free(region);
        return invalid_shared;
    }
    // We allocate the shared memory buffer such that its words are correctly aligned.
    if (posix_memalign(&(firstSegment->freeSpace), align, size) != 0) {
        free(firstSegment);
        free(region);
        return invalid_shared;
    }
    // Initialize the region with 0s
    memset(firstSegment->freeSpace, 0, size);
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
    firstSegment->id    = 1;
    void* address = (void*)((unsigned long)firstSegment->id << 48);
    // printf("Created region with start address %p\n",address);
    firstSegment->size  = size;
    firstSegment->align = align;
    firstSegment->allocator = 0;
    firstSegment->accessible = true;
    firstSegment->fakeSpace = address;
    firstSegment->deleted = false;
    region->align       = align;
    region->segments.elements[firstSegment->id] = firstSegment;
    region->start = address;
    region->globalVersion = 0;
    region->latestTransactionId = 1;
    return region;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
**/
void tm_destroy(shared_t shared) {
    Region* region = (Region *) shared;
    // Free all the segments of the region.
    for(int i = 0; i < 65536; i++) {
        segment_node* currentSegment = region->segments.elements[i];
        if(currentSegment != NULL) {
            free(currentSegment->freeSpace);
            free(currentSegment->locks);
            free(currentSegment);
        }
    }
    free(region->segments.elements);
    free(region->segments.locks);
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
    return ((Region*)shared)->segments.elements[1]->size;
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
    newTransaction->readSetHead = malloc(sizeof(write_set_node)*SET_START_SIZE);;
    newTransaction->readSetTail = newTransaction->readSetHead;
    newTransaction->writeSetHead = malloc(sizeof(write_set_node)*SET_START_SIZE);
    newTransaction->writeSetTail = newTransaction->writeSetHead;
    newTransaction->allocListHead = NULL;
    newTransaction->allocListTail = NULL;
    newTransaction->freeListHead = NULL;
    newTransaction->freeListTail = NULL;
    newTransaction->writeSetSize = 0;
    newTransaction->readSetSize = 0;
    newTransaction->region = region;
    newTransaction->version = region->globalVersion; // The transaction starts with the current global version.
    newTransaction->isReadOnly = is_ro;
    // Fetch and increment the latest transaction id
    // newTransaction->writeSetBloom = (bloom*)malloc(sizeof(bloom));
    // bloom_init2(newTransaction->writeSetBloom, 1000, 0.01);
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
        lock_t** locksToAcquire = getLocks(transaction);
        if(acquireLocks(locksToAcquire, transaction->writeSetSize, transaction->id)) {
            atomic_ulong newVersion = ++region->globalVersion;
            if(transaction->version + 1 != newVersion) {
                read_set_node * currentReadSetNode = transaction->readSetHead;
                while (currentReadSetNode != transaction->readSetTail) {
                    lock_node * lockNode = getLockNode(currentReadSetNode->segment, currentReadSetNode->offset);
                    if ( lockNode->version > transaction->version) {
                        releaseLocks(locksToAcquire, transaction->writeSetSize, transaction->id);
                        clearSets(transaction,false);
                        free(locksToAcquire);
                        return false;
                    }
                    if(lock_is_locked_byAnotherThread_holder(&lockNode->lock, transaction->id)) {
                        releaseLocks(locksToAcquire, transaction->writeSetSize, transaction->id);
                        clearSets(transaction,false);
                        free(locksToAcquire);
                        return false;
                    }
                    currentReadSetNode = currentReadSetNode->next;
                }
            }
            write_set_node* currentWriteSetNode = transaction->writeSetHead;
            // Iterate over the write set and write the new values to shared memory and update their version
            while (currentWriteSetNode != transaction->writeSetTail) {
                // Update the version of the address.
                getLockNode(currentWriteSetNode->segment, currentWriteSetNode->offset)->version = newVersion;
                // Write the new value to shared memory.
                memcpy(currentWriteSetNode->segment->freeSpace + currentWriteSetNode->offset * region->align,
                       currentWriteSetNode->value,
                       region->align);
                currentWriteSetNode = currentWriteSetNode->next;
            }
            if(transaction->allocListHead != NULL) allocateSegments(transaction,region);
            if(transaction->freeListHead != NULL) freeSegments(transaction,region);
            // Release all the locks.
            releaseLocks(locksToAcquire, transaction->writeSetSize, transaction->id);
            free(locksToAcquire);
        }
        else{
            // Abort the transaction.
            free(locksToAcquire);
            clearSets(transaction,false);
            return false;
        }
    }
    clearSets(transaction,true);
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
    segment_node* segment = findSegment(region, (void*)source,transaction);
    if(segment == NULL){
        clearSets(transaction,false);
        return false;
    }
    size_t currentWord = 0;
    void* actualSource = segment->freeSpace + (source - segment->fakeSpace);
    for(;currentWord < size / region->align;currentWord++){
        void* currentTarget = (void*) target + currentWord * region->align;
        void* currentSource = (void*) actualSource + currentWord * region->align;

        write_set_node* writeSetNode = NULL;

        size_t offset = (size_t) (currentSource - segment->freeSpace) / region->align;
        lock_node* lockNode = getLockNode(segment, offset);
        unsigned long version = lockNode->version;

        if(version != lockNode->version){
            clearSets(transaction,false);
            return false;
        }
        if (lock_is_locked(&lockNode->lock)) {
            clearSets(transaction,false);
            return false;
        }
        if (lockNode->version > transaction->version) {
            clearSets(transaction,false);
            return false;
        }

        if (!transaction->isReadOnly) {
            writeSetNode = checkWriteSet(region, transaction, currentSource);
        }
        if(writeSetNode == NULL){
            memcpy(currentTarget, currentSource, region->align);
        }
        else{
            memcpy(currentTarget, writeSetNode->value, region->align);
        }
        if(version != lockNode->version){
            clearSets(transaction,false);
            return false;
        }
        if (lock_is_locked(&lockNode->lock)) {
            clearSets(transaction,false);
            return false;
        }
        if (lockNode->version > transaction->version) {
            clearSets(transaction,false);
            return false;
        }
        // Then we need to add the read set node to the read set.
        if(!transaction->isReadOnly){
            addReadSet(transaction, segment, offset);
        }
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
    segment_node* segment = findSegment(region, target,transaction);
    if(segment == NULL){
        clearSets(transaction,false);
        return false;
    }
    size_t currentWord = 0;
    void* actualTarget = segment->freeSpace + (target - segment->fakeSpace);
    for(;currentWord < size / region->align; currentWord++){
        void* currentTarget = (void*) actualTarget + currentWord * region->align;
        void* currentSource = (void*) source + currentWord * region->align;
        write_set_node* writeSetNode = checkWriteSet(region, transaction, currentTarget);
        if(writeSetNode != NULL){
            memcpy(writeSetNode->value, currentSource, region->align);
            continue;
        }
        unsigned long offset = (size_t) ((void*) currentTarget - segment->freeSpace) / region->align;
        void* value = aligned_alloc(region->align, region->align);
        if(value == NULL){
            clearSets(transaction,false);
            return false;
        }
        memcpy(value, currentSource, region->align);
        addWriteSet(transaction, segment, offset, value, region->align);
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
alloc_t tm_alloc(shared_t shared, tx_t tx, size_t size, void** target) {
    Region* region = (Region *) shared;
    transaction* transaction = getTransaction(region, tx);
    // First try to find an empty space in the segment array
    int id = findEmptySegment(&region->segments);
    if(id == -1) {
        return nomem_alloc;
    }
    void* address = (void*)((unsigned long)id << 48);
    // printf("Fake Address: %p\n", address);
    segment_node* newSegment = (segment_node *) malloc(sizeof(segment_node));
    if (unlikely(!newSegment)) {
        atomic_store((region->segments.locks+id), false);
        return nomem_alloc;
    }
    if (posix_memalign(&(newSegment->freeSpace), region->align, size) != 0) {
        free(newSegment);
        atomic_store((region->segments.locks+id), false);
        return nomem_alloc;
    }
    memset(newSegment->freeSpace, 0, size); // Set all bytes to 0
    newSegment->fakeSpace = address;
    size_t wordCount = size / region->align;
    newSegment->lock_size = (wordCount / TSM_WORDS_PER_LOCK) + (wordCount % TSM_WORDS_PER_LOCK == 0 ? 0 : 1);
    newSegment->locks = (lock_node *) malloc(sizeof(lock_node) * newSegment->lock_size);
    newSegment->accessible = false;
    newSegment->id = id;
    newSegment->deleted = false;
    newSegment->allocator = transaction->id;
    if(unlikely(!newSegment->locks)) {
        free(newSegment->freeSpace);
        free(newSegment);
        atomic_store((region->segments.locks+id), false);
        return nomem_alloc;
    }
    atomic_ulong globalVersion = region->globalVersion;
    for (int i = 0; i < newSegment->lock_size; i++) {
        lock_init(&((newSegment->locks + i)->lock));
        newSegment->locks[i].version = globalVersion;
    }
    if(transaction->allocListHead == NULL){
        transaction->allocListHead = (segment_ll*) malloc(sizeof(segment_ll));
        if(unlikely(!transaction->allocListHead)){
            free(newSegment->locks);
            free(newSegment->freeSpace);
            free(newSegment);
            atomic_store((region->segments.locks+id), false);
            return nomem_alloc;
        }
        transaction->allocListHead->segmentNode = newSegment;
        transaction->allocListHead->next = NULL;
        transaction->allocListTail = transaction->allocListHead;

    }
    else {
        segment_ll *tail = transaction->allocListTail;
        tail->next = (segment_ll *) malloc(sizeof(segment_ll));
        if(unlikely(!tail->next)){
            free(newSegment->locks);
            free(newSegment->freeSpace);
            free(newSegment);
            atomic_store((region->segments.locks+id), false);
            return nomem_alloc;
        }
        tail->next->segmentNode = newSegment;
        tail->next->next = NULL;
        transaction->allocListTail = tail->next;
    }
    region->segments.elements[id] = newSegment;
    atomic_store((region->segments.locks+id), false);
    // printf("Allocated segment %d\n", id);
    memcpy(target, &newSegment->fakeSpace, sizeof(void*));
    return success_alloc;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment to deallocate
 * @return Whether the whole transaction can continue
**/
bool tm_free(shared_t shared, tx_t tx, void* target) {
    // printf("Freeing segment %p\n", target);
    Region* region = (Region *) shared;
    transaction* transaction = getTransaction(region, tx);
    // printf("T%lu: TM_Free called.\n",transaction->id);
    segment_node* segment = findSegment(region, target,transaction);
    if(segment == NULL){
        clearSets(transaction,false);
        return false;
    }
    if(transaction->freeListHead == NULL){
        transaction->freeListHead = (segment_ll*) malloc(sizeof(segment_ll));
        if(unlikely(!transaction->freeListHead)){
            return false;
        }
        transaction->freeListHead->segmentNode = segment;
        transaction->freeListHead->next = NULL;
        transaction->freeListTail = transaction->freeListHead;
    }
    else {
        segment_ll *tail = transaction->freeListTail;
        tail->next = (segment_ll *) malloc(sizeof(segment_ll));
        if(unlikely(!tail->next)){
            return false;
        }
        tail->next->segmentNode = segment;
        tail->next->next = NULL;
        transaction->freeListTail = tail->next;
    }
    // printf("T%lu: TM_Free finished.\n",transaction->id);
    return true;
}
