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
    region->allocHead      = firstSegment; // The region starts with one non-deletable segment.
    region->allocTail      = firstSegment; // The region starts with one non-deletable segment.
    region->align       = align;
    region->start       = firstSegment->freeSpace;
    region->transactions = NULL; // No transactions yet.
    region->globalVersion = 0;
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
    if (unlikely(!shared)) {
        return;
    }
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
    newTransaction->next = NULL; // As this is the newest transaction in the region, it has no next transaction.
    newTransaction->isReadOnly = is_ro;
    lock_acquire_blocking(region->globalLock,-1);
    // Is this the first transaction of the region?
    if(region->transactions == NULL){
        // If yes, then this transaction is the first transaction of the region.
        newTransaction->id = 0;
        newTransaction->prev = NULL;
        region->transactions = newTransaction;
    }
    else{
        // If no, then this transaction is the next transaction of the region.
        newTransaction->id = region->transactions->id + 1;
        newTransaction->prev = region->transactions;
        region->transactions->next = newTransaction;
        region->transactions = newTransaction;
    }
    // print transaction id
    printf("Transaction %d started\n", newTransaction->id);
    lock_release(region->globalLock,-1); // Release the global lock.
    return newTransaction->id;
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
**/
bool tm_end(shared_t shared, tx_t tx) {
    Region* region = (Region *) shared;
    if(unlikely(!region)) {
        return false;
    }
    transaction* transaction = getTransaction(region, tx);
    if(unlikely(!transaction)) {
        return false;
    }
    // **********************
    // READ SET COMMIT CHECKS
    // **********************
    // Validating the read set is not difficult, for each element in the read set we do the following:
    // 1. Check if the word's version is still the same as the one we read.
    // 2. If it is then we need to also check if the lock is still unlocked.
    // If these two conditions are met, then the read set is valid
    read_set_node * currentReadSetNode = transaction->readSetHead;
    while(currentReadSetNode != NULL){
        if(currentReadSetNode->version != getVersion(currentReadSetNode->segment,currentReadSetNode->offset)){
            // If the version of the lock node is different from the version of the read set node, then
            // the transaction failed. As this transaction was working with old data, we need to abort it.
            return false;
        }
        // We have to also check if the lock of this read set node is currently held by another transaction.
        else{
            lock_t lock = getLock(currentReadSetNode->segment,currentReadSetNode->offset);
            if(lock_is_locked(&lock)){
                // If the lock is held by another transaction, then we need to abort this transaction.
                return false;
            }
        }
        currentReadSetNode = currentReadSetNode->next;
    }
    // If the transaction is not read-only, then the write set also should be checked
    // **********************
    // WRITE SET COMMIT CHECKS
    // **********************
    // Validating the write set is more involved than the read set, for each element in the write set we do the following:
    // 1. Acquire the locks of the write set nodes. To prevent deadlocks, we need to acquire the locks in the
    // same order always, so we will sort the write set nodes by the address of their locks
    // 2. After acquiring the locks, we need to check if the version is still the same as the one that we have in the write set.
    // 3. If the above two conditions are met then we copy the data saved in the write set nodes to the shared memory region/segment.
    // 4. Then we need to increment the version numbers of each word we have written to.
    // 5. We also need to increment the global version number
    // 6. Finally, we need to release the locks of the write set nodes and we are done.
    if(!transaction->isReadOnly){
        // Sort the write set nodes by the address of their locks.
        sortWriteSet(transaction);
        // Acquire the locks of the write set nodes.
        if(!acquireLocks(transaction)) return false;
        // Check if the version is still the same as the one that we have in the write set.
        write_set_node* currentWriteSetNode = transaction->writeSetHead;
        while(currentWriteSetNode != NULL){
            if(currentWriteSetNode->version != getVersion(currentWriteSetNode->segment,currentWriteSetNode->offset)){
                // If the version of the lock node is different from the version of the write set node, then
                // the transaction failed. As this transaction was working with old data, we need to abort it.
                // Release the locks of the write set nodes.
                releaseLocks(transaction);
                return false;
            }
            currentWriteSetNode = currentWriteSetNode->next;
        }
        // Copy the data saved in the write set nodes to the shared memory region/segment.
        currentWriteSetNode = transaction->writeSetHead;
        while(currentWriteSetNode != NULL){
            memcpy(currentWriteSetNode->segment->freeSpace + (currentWriteSetNode->offset)*region->align,
                   currentWriteSetNode->newValue,
                   region->align);
            currentWriteSetNode = currentWriteSetNode->next;
        }
        // Increment the version numbers of each word we have written to.
        incrementVersion(transaction, region);
        // Atomically increment the global version number.
        __sync_fetch_and_add(&region->globalVersion, 1);
        // Release the locks of the write set nodes.
        releaseLocks(transaction);
    }
    // If we reach this point, then the transaction succeeded.
    // Destroy the transaction and free the memory.
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
    // Before going on with the read operation we have to some fail checks.
    /***********************
     * * Fail checks start *
     ***********************/
    transaction* transaction = getTransaction(region, tx);
    if (unlikely(!transaction)) { // No transaction could be found with the given id
        return false;
    }
    // Check if size is a positive multiple of the alignment. As this is one of the requirements.
    if (unlikely(size % region->align != 0 || size <= 0)) {
        clearSets(transaction);
        return false;
    }
    // We need to check if the memory location we are trying to read from is aligned.
    // This is a requirement as we don't want to let the user start reading from middle of a word.
    if ((size_t) source % region->align != 0) {
        // If no, then the transaction is aborted.
        // This is because the memory location we are trying to read from is not aligned.
        // Before aborting clear the read and write sets of the transaction.
        clearSets(transaction);
        return false;
    }
    // Same for the target memory location.
    if ((size_t) target % region->align != 0) {
        // If no, then the transaction is aborted.
        // This is because the memory location we are trying to read from is not aligned.
        // Before aborting clear the read and write sets of the transaction.
        clearSets(transaction);
        return false;
    }
    // Now, we need to find which segment the memory location we are trying to read from is in.
    segment_node* segment = findSegment(region, (void*)source);
    if(segment == NULL){
        // If no segment is found, then the memory location we are trying to write to is not in the shared memory.
        clearSets(transaction);
        return false;
    }
    // Check if source + size is inside this segment
    if((void*) source + size > segment->freeSpace + segment->size){
        // If no, then the transaction is aborted.
        // This is because we are trying to read from a memory location that is not allocated.
        // Before aborting clear the read and write sets of the transaction.
        clearSets(transaction);
        return false;
    }
    /***********************
     * * Fail checks end * *
     ***********************/
    // If we reach this point, then the read operation can be performed.
    // Note that we are required to read 'size' bytes from the memory location 'source'
    // Because we can read one and write one word at a time, we need to read (size/align) amount of words.
    size_t currentWord = 0;
    for(;currentWord < size / region->align;currentWord++){
        void* currentTarget = (void*) target + currentWord * region->align;
        void* currentSource = (void*) source + currentWord * region->align; // We start from the source address
        // which is the first word to be read, then in each iteration we add the size of a word to the address.
        /*****************************************************************************************************/
        // Check if the transaction is read-only.
        if (!transaction->isReadOnly) {
            // The current transaction is NOT read-only.
            // Then we first check if the memory location we are trying to read from already exists in the write set.
            // As it might be the case that the transaction is trying to read from a memory location that it has already written to.
            write_set_node *writeSetNode = checkWriteSet(region, transaction, currentSource);
            if (writeSetNode != NULL) {
                // If yes, then we can just copy the value from the write set as this is the value we have previously written.
                // The 'new value' field in the node is the value/word this transaction has previously written to this memory location.
                memcpy(currentTarget, writeSetNode->newValue, region->align);
                continue; // No need to check for rest, move on to the next word
            }
        }
        // We get here if the transaction is read-only or the memory location
        // we are trying to read from does not exist in the write set.
        // Then we check if the memory location we are trying to read from already exists in the read set.
        // As it might be the case that the transaction is trying to read from a memory location that it has already read from.
        // To keep consistency, we want to read the same value from the same memory location.
        read_set_node * readSetNode = checkReadSet(region, transaction, currentSource);
        if(readSetNode != NULL){
            // If yes, then we can just copy the old value we have read previously.
            // The 'value' field in the node is the value/word this transaction has previously read from this memory location.
            memcpy(currentTarget, readSetNode->value, region->align);
            continue; // No need to check for the rest, move on to the next word
        }
        // If no, then we need to read from the shared memory as it is our very first time reading from this memory location.
        // First we need to find the offset of the memory location we are trying to read from.
        // i.e. we need to find how many words away we are from the segment's start address.
        size_t offset = (size_t) ((void*) currentSource - segment->freeSpace) / region->align;
        // Check if the memory location is unlocked
        lock_t lock = getLock(segment, offset);
        if (lock_is_locked(&lock)) {
            // If no, then the transaction is aborted.
            // This is because the memory location we are trying to read from is being written to by another transaction.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // Then we need to get the version number of the word we are trying to write.
        uint64_t version = getVersion(segment, offset);
        // Check if this version is greater than the transaction's version.
        if (version > transaction->version) {
            // If yes, then the transaction is aborted.
            // This is because the memory location we are trying to read from,
            // has been written by another transaction since the transaction started.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // Then we need to read the value of the word.
        // because we are reading one word at a time, size is always equal to the alignment.
        // We need to allocate a new memory location to store the value of the word we have read in the read set.
        void* value = aligned_alloc(region->align,region->align);
        if(value == NULL){
            // If the malloc fails, then we return false.
            clearSets(transaction);
            return false;
        }
        memcpy(value, currentSource, region->align); // we save the value we read in case we read it again.
        // Then we need to add the read set node to the read set.
        addReadSet(transaction, segment, offset, version, value);
        // Finally, we copy the value/word we have read to the target memory location.
        memcpy(currentTarget, value, region->align);
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
    // Before going on with the write operation we have to some fail checks.
    /***********************
     * * Fail checks start *
     ***********************/
    transaction* transaction = getTransaction(region, tx);
    if (unlikely(!transaction)) { // No transaction could be found with the given id
        return false;
    }
    // Check if the transaction is read-only.
    if (unlikely(transaction->isReadOnly)) {
        // If yes, then the transaction is aborted.
        // This is because the transaction is read-only.
        // Before aborting clear the whole read and write set of the transaction.
        clearSets(transaction);
        return false;
    }
    // Check if the size is a positive multiple of the alignment.
    if (unlikely(size % region->align != 0 || size <= 0)) {
        // If no, then the transaction is aborted.
        // This is because the size will be divided by the alignment to dictate how many words we are going to write.
        // So the size must be a positive multiple of the alignment.
        // Before aborting clear the whole read and write set of the transaction.
        clearSets(transaction);
        return false;
    }
    //  We need to check if the memory location we are trying to write to is aligned.
    if ((size_t) target % region->align != 0) {
        // If no, then the transaction is aborted.
        // This is because if the memory location is not aligned we will
        // start writing from middle of a word.
        // Before aborting clear the read and write sets of the transaction.
        clearSets(transaction);
        return false;
    }
    // Same reasoning for the source
    if ((size_t) source % region->align != 0) {
        clearSets(transaction);
        return false;
    }
    // Now, we need to find which segment the memory location we are trying to write to is in.
    segment_node* segment = findSegment(region, target);
    if(segment == NULL){
        // If no segment is found, then the memory location we are trying to write to is not in the shared memory.
        clearSets(transaction);
        return false;
    }
    // Check if target + size is inside this segment
    if((void*) target + size > segment->freeSpace + segment->size){
        // If no, then the transaction is aborted.
        // This is because the memory location we don't have enough memory.
        // Before aborting clear the read and write sets of the transaction.
        clearSets(transaction);
        return false;
    }
    /***********************
    * * Fail checks end * *
    ***********************/
    // If we reach this point, then the write operation can be performed.
    // Note that we are required to write 'size' bytes from the source to the target.
    // As we are writing one word at a time, we need to divide the size by the alignment to find out how many words we are going to write.
    size_t currentWord = 0;
    for(;currentWord < size / region->align; currentWord++){
        void* currentTarget = (void*) target + currentWord * region->align; // We start from the target, which is the memory location
        // of the first word we want to write. At each iteration we move to the next word by adding the alignment to the current target.
        void* currentSource = (void*) source + currentWord * region->align;
        /*****************************************************************************************************/
        // Check if the memory location we are trying to write to already exists in the write set.
        write_set_node* writeSetNode = checkWriteSet(region, transaction, currentTarget);
        if(writeSetNode != NULL){
            // If yes, then we can just copy the new value to the write set.
            // The 'newValue' field in the write set node corresponds to the new value/word that this transaction wants to write.
            // When the same transactions commits, the value in the write set will be written to the shared memory.
            // If the transaction tries to read from the same memory location before it commits, we will return the value from the write set
            // Check the tm_read function for more details on how we do that.
            memcpy(writeSetNode->newValue, currentSource, region->align);
            continue; // No need to check for the rest of the conditions for this word.
        }
        // If no, then we need to read from the shared memory, as it is the first time we are trying to write to this memory location.
        // First we need to find the offset, i.e. how many words down we are from the current segment.
        size_t offset = (size_t) ((void*) currentTarget - segment->freeSpace) / region->align;
        // Check if the memory location is unlocked
        lock_t lock = getLock(segment, offset);
        if (lock_is_locked(&lock)) {
            // If no, then the transaction is aborted.
            // This is because the memory location we are trying to write to is being written to by another transaction.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // Then we need to get the version number of the word we are trying to write.
        uint64_t version = getVersion(segment, offset);
        // Check if this version is greater than the transaction's version.
        if (version > transaction->version) {
            // If yes, then the transaction is aborted.
            // This is because the memory location we are trying to write to, has been written by another transaction.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // All checks have passed, we can write to the memory location.
        // We save the old value in the write node, so we create space for that.
        void* value = aligned_alloc(region->align,region->align);
        if(value == NULL){
            // If the malloc fails, then we return false.
            clearSets(transaction);
            return false;
        }
        // This way we have saved the old value of the word.
        memcpy(value, currentTarget, region->align);
        // Now we make space in the write set node for the new value of the word.
        void* newValue = aligned_alloc(region->align,region->align);
        if(newValue == NULL){
            // If the malloc fails, then we return false.
            clearSets(transaction);
            return false;
        }
        memcpy(newValue, currentSource, region->align);
        // Then we need to add the write set node to the transaction.
        addWriteSet(transaction, segment, offset, version, value, newValue);
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
        return abort_alloc;
    }
    // First create a new segment node
    segment_node* newSegment = (segment_node *) malloc(sizeof(segment_node));
    if (unlikely(!newSegment)) {
        return abort_alloc;
    }
    // Allocate memory for the segment with given alignment and size
    if (posix_memalign(&(newSegment->freeSpace), region->align, size) != 0) {
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
    size_t globalVersion = region->globalVersion;
    for (int i = 0; i < newSegment->lock_size; i++) {
        lock_init(&((newSegment->locks + i)->lock));
        newSegment->locks[i].version = globalVersion;
    }
    // Lock the region, so no other transaction can allocate memory at the same time.
    lock_acquire_blocking(region->globalLock,-1);
    // Add this segment to the list of segments of the region.
    newSegment->prev = region->allocTail; // The prev pointer of this segment points to the last segment of the region.
    newSegment->next = NULL; // The next pointer of this segment is NULL. As it is currently the latest segment of the region.
    newSegment->size  = size;
    newSegment->id    = region->allocTail->id + 1;
    region->allocTail->next = newSegment; // The next pointer of the last segment of the region points to this segment.
    region->allocTail = newSegment; // Latest segment of the region is updated to be this segment.
    // Unlock the region.
    lock_release(region->globalLock,-1);
    *target = newSegment->freeSpace;
    return success_alloc;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment to deallocate
 * @return Whether the whole transaction can continue
**/
bool tm_free(shared_t shared, tx_t unused(tx), void* target) {
    // First find the segment node corresponding to the given address.
    Region* region = (Region *) shared;
    segment_node* currentSegment = findSegment(region, target);
    if (currentSegment == NULL) {
        return false;
    }
    // If the segment's id is 0 then it is the first segment allocated thus we cannot free it.
    if (currentSegment->id == 0) {
        return false;
    }
    // Lock the region, so no other transaction can allocate memory at the same time.
    lock_acquire_blocking(region->globalLock,-1);
    // Remove the segment node from the list of segments of the region.
    if (currentSegment->prev != NULL) {
        currentSegment->prev->next = currentSegment->next;
    }
    if (currentSegment->next != NULL) {
        currentSegment->next->prev = currentSegment->prev;
    }
    lock_release(region->globalLock,-1);
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

// Return the region version
uint64_t tm_version(shared_t shared) {
    Region* region = (Region *) shared;
    return region->globalVersion;
}
