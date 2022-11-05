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
#include <math.h>
#include <FlexLexer.h>

#include "macros.h"
#include "tsm_types.h"

/****************************************************************************************/
/*                                                                                      */
/*  START OF HELPER FUNCTION DECLARATIONS                                               */
/*                                                                                      */
/****************************************************************************************/

/** This function is used to get the transaction with the given id from the region.
 * @param region The region to get the transaction from.
 * @param id The id of the transaction to get.
 * @return The transaction with the given id.
 **/
transaction* getTransaction(Region *pRegion, tx_t tx) {
    transaction* currentTransaction = pRegion->transactions;
    while (currentTransaction != NULL) {
        if (currentTransaction->id == tx) {
            return currentTransaction;
        }
        currentTransaction = currentTransaction->prev;
    }
    return NULL;
}

/** This function is used to add a new readSet node to the given transaction.
 * @param transaction The transaction to add the readSet node to.
 * @param segment The memory segment that we are reading from.
 * @param offset The number of alignments from the start of the segment.
 * @param version The current version of the word that we are reading.
 * @param value The value of the word that we are reading at read time
 **/
void addReadSet(transaction* pTransaction, segment_node* pSegment, size_t offset, uint64_t version, void* value) {
    read_set_node * newReadSet = (read_set_node *) malloc(sizeof(read_set_node));
    newReadSet->segment = pSegment;
    newReadSet->offset = offset;
    newReadSet->version = version;
    newReadSet->value = value;
    newReadSet->next = NULL;
    if (pTransaction->readSetHead == NULL) {
        pTransaction->readSetHead = newReadSet;
        pTransaction->readSetTail = newReadSet;
        pTransaction->readSetSize = 1;
    }
    else {
        pTransaction->readSetTail->next = newReadSet;
        pTransaction->readSetTail = newReadSet;
        pTransaction->readSetSize++;
    }
}

/** This function is used to check if a given address is inside the read set of a given transaction.
 * @param shared The shared memory region that the address belongs to.
 * @param transaction The transaction to check the read set of.
 * @param address The address to check.
 * @return The read set node that contains the address, or NULL if the address is not in the read set.
 **/
read_set_node* checkReadSet(Region* pRegion, transaction* pTransaction, void* address) {
    read_set_node *currentReadSet = pTransaction->readSetHead;
    while (currentReadSet != NULL) {
        if (currentReadSet->segment->freeSpace + currentReadSet->offset * pRegion->align == address) {
            return currentReadSet;
        }
        currentReadSet = currentReadSet->next;
    }
    return NULL;
}

/** This function is used to find the segment that contains the given address.
 * @param shared The shared memory region that the address belongs to.
 * @param address The address to find the segment of.
 * @return The segment that contains the address, or NULL if the address is not in any segment.
 **/
segment_node* findSegment(Region* pRegion, void* address) {
    segment_node *currentSegment = pRegion->allocHead;
    while (currentSegment != NULL) {
        if (currentSegment->freeSpace <= address && address < currentSegment->freeSpace + currentSegment->size) {
            return currentSegment;
        }
        currentSegment = currentSegment->next;
    }
    return NULL;
}

/** This function is used to add a new writeSet node to the given transaction.
 * @param transaction The transaction to add the writeSet node to.
 * @param segment The memory segment that we are writing to.
 * @param offset The number of alignments from the start of the segment.
 * @param version The current version of the word that we are writing.
 * @param value The value of the word that we are writing.
 * @param newValue The new value of the word that we are writing.
 **/
void addWriteSet(transaction* pTransaction,
                 segment_node* pSegment,
                 size_t offset,
                 uint64_t version,
                 void* value,
                 void* newValue) {
    write_set_node *newWriteSet = (write_set_node *) malloc(sizeof(write_set_node));
    newWriteSet->segment = pSegment;
    newWriteSet->offset = offset;
    newWriteSet->version = version;
    newWriteSet->newVersion = version + 1;
    newWriteSet->value = value;
    newWriteSet->newValue = newValue;
    newWriteSet->next = NULL;
    if (pTransaction->writeSetHead == NULL) {
        pTransaction->writeSetHead = newWriteSet;
        pTransaction->writeSetTail = newWriteSet;
        pTransaction->writeSetSize = 1;
    } else {
        pTransaction->writeSetTail->next = newWriteSet;
        pTransaction->writeSetTail = newWriteSet;
        pTransaction->writeSetSize++;
    }
}

/** This function is used to check if a given address is inside the write set of a given transaction.
 * @param shared The shared memory region that the address belongs to.
 * @param transaction The transaction to check the write set of.
 * @param address The address to check.
 * @return The write set node that contains the address, or NULL if the address is not in the write set.
 **/
write_set_node* checkWriteSet(Region* pRegion, transaction* pTransaction, void* address) {
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    while (currentWriteSet != NULL) {
        if (currentWriteSet->segment->freeSpace + currentWriteSet->offset * pRegion->align == address) {
            return currentWriteSet;
        }
        currentWriteSet = currentWriteSet->next;
    }
    return NULL;
}

/** This function is used to get the lock associated with a memory location in a given segment
 * @param segment The segment to get the lock from.
 * @param offset The number of alignments from the start of the segment.
 * @return The lock associated with the memory location.
 **/
shared_lock_t getLock(segment_node* pSegment, size_t offset) {
    // Remember that each segment has one lock per TSM_WORDS_PER_LOCK words.
    // the lock at index 'i' protects the words at indices i * TSM_WORDS_PER_LOCK to (i + 1) * TSM_WORDS_PER_LOCK - 1 (inclusive.)
    return pSegment->locks[offset / TSM_WORDS_PER_LOCK].lock;
}

/** This function is used to get the version number of a word in a given segment.
 * @param segment The segment to get the version number from.
 * @param shared The shared memory region that the segment belongs to.
 * @param offset The number of alignments from the start of the segment.
 * @return The version number of the word.
 **/
uint64_t getVersion(segment_node* pSegment, Region* unused(pRegion), size_t offset) {
    return pSegment->locks[offset / TSM_WORDS_PER_LOCK].version;
}

/** This function sorts the write set of the given transaction
 * by increasing order of the address of locks it needs to acquire
 * @param pTransaction pointer to the transaction
**/

void sortWriteSet(transaction *pTransaction) {
    write_set_node *current = pTransaction->writeSetHead;
    write_set_node *index = NULL;
    shared_t tempValue;
    shared_t tempNewValue;
    uint64_t tempVersion;
    uint64_t tempNewVersion;
    segment_node *tempSegment;
    size_t tempOffset;

    while (current != NULL) {
        index = current->next;

        while (index != NULL) {
            shared_lock_t currentLock = getLock(current->segment, current->offset);
            shared_lock_t indexLock = getLock(index->segment, index->offset);
            if (&currentLock >
                &indexLock) {
                tempValue = current->value;
                tempNewValue = current->newValue;
                tempVersion = current->version;
                tempNewVersion = current->newVersion;
                tempSegment = current->segment;
                tempOffset = current->offset;

                current->value = index->value;
                current->newValue = index->newValue;
                current->version = index->version;
                current->newVersion = index->newVersion;
                current->segment = index->segment;
                current->offset = index->offset;

                index->value = tempValue;
                index->newValue = tempNewValue;
                index->version = tempVersion;
                index->newVersion = tempNewVersion;
                index->segment = tempSegment;
                index->offset = tempOffset;
            }

            index = index->next;
        }

        current = current->next;
    }

}

/** This function returns all the locks in the write set of the given transaction
 * @param pTransaction pointer to the transaction
 * @param pLocks pointer to the array of locks
 * @param pLocksSize pointer to the size of the array of locks
 * @return true if successful, false otherwise
 **/
bool getLocks(transaction *pTransaction, shared_lock_t **pLocks, size_t *pLocksSize) {
    write_set_node *current = pTransaction->writeSetHead;
    size_t locksSize = 0;
    shared_lock_t *locks = (shared_lock_t *) malloc(sizeof(shared_lock_t) * pTransaction->writeSetSize);
    if (locks == NULL) {
        return false;
    }
    while (current != NULL) {
        locks[locksSize] = getLock(current->segment, current->offset);
        locksSize++;
        current = current->next;
    }
    *pLocks = locks;
    *pLocksSize = locksSize;
    return true;
}

/** This function is used to acquire the locks associated with the write set of a given transaction.
 * @param transaction The transaction to acquire the locks for.
 * @return true if the locks were acquired successfully, or false if the locks couldn't be acquired.
 **/
bool acquireLocks(transaction* pTransaction) {
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    sortWriteSet(pTransaction);
    shared_lock_t *locks;
    size_t locksSize;
    if (!getLocks(pTransaction, &locks, &locksSize)) {
        return false;
    }
    for (size_t i = 0; i < locksSize; i++) {
        if (!shared_lock_acquire(&locks[i])) {
            for (size_t j = 0; j < i; j++) {
                shared_lock_release(&locks[j]);
            }
            free(locks);
            return false;
        }
    }
    free(locks);
    return true;
}

/** This function is used to release the locks associated with the write set of a given transaction.
 * @param transaction The transaction to release the locks for.
 * @return true if the locks were released successfully, or false if the locks couldn't be released.
 **/
bool releaseLocks(transaction* pTransaction) {
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    sortWriteSet(pTransaction);
    shared_lock_t *locks;
    size_t locksSize;
    if (!getLocks(pTransaction, &locks, &locksSize)) {
        return false;
    }
    for (size_t i = 0; i < locksSize; i++) {
        shared_lock_release(&locks[i]);
    }
    free(locks);
    return true;
}

/** This function clears the read and write sets of the given transaction.
 * @param transaction The transaction to clear the read and write sets of.
 * @return true if the read and write sets were cleared successfully, or false if they couldn't be cleared.
 **/
bool clearSets(transaction* pTransaction) {
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    write_set_node *nextWriteSet;
    while (currentWriteSet != NULL) {
        nextWriteSet = currentWriteSet->next;
        free(currentWriteSet->newValue);
        free(currentWriteSet->value);
        free(currentWriteSet);
        currentWriteSet = nextWriteSet;
    }
    pTransaction->writeSetHead = NULL;
    pTransaction->writeSetTail = NULL;
    pTransaction->writeSetSize = 0;

    read_set_node *currentReadSet = pTransaction->readSetHead;
    read_set_node *nextReadSet;
    while (currentReadSet != NULL) {
        nextReadSet = currentReadSet->next;
        free(currentReadSet->value);
        free(currentReadSet);
        currentReadSet = nextReadSet;
    }

    return true;
}
/**
 ***************************************************************************************
 * END OF HELPER FUNCTIONS
 ***************************************************************************************
 **/

/** Create (i.e. allocate + init) a new shared memory region, with one first non-free-able allocated segment of the requested size and alignment.
 * @param size  Size of the first shared segment of memory to allocate (in bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the shared memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
**/
shared_t tm_create(size_t size, size_t align) {
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
    // Initialize lock nodes in the segment, as each lock is associated with TSM_WORDS_PER_LOCK words, we need to initialize
    // size / align / TSM_WORDS_PER_LOCK locks.
    // But as this can give us a decimal number, we need to round it up to the next integer.
    firstSegment->lock_size = (int) ceil((double) size / align / TSM_WORDS_PER_LOCK);
    firstSegment->locks = (lock_node *)malloc(sizeof(lock_node) * firstSegment->lock_size);
    if(unlikely(!firstSegment->locks)) {
        free(firstSegment->freeSpace);
        free(firstSegment);
        free(region);
        return invalid_shared;
    }
    for (int i = 0; i < firstSegment->lock_size; i++) {
        shared_lock_init(&((firstSegment->locks + i)->lock));
        (firstSegment->locks + i)->version = 0;
    }
    firstSegment->size  = size;
    firstSegment->id    = 0;
    region->allocHead      = firstSegment; // The region starts with one non-deletable segment.
    region->allocTail      = firstSegment; // The region starts with one non-deletable segment.
    region->align       = align;
    region->start       = firstSegment->freeSpace;
    region->transactions = NULL; // No transactions yet.
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
        for (int i = 0; i < currentSegment->size / region->align / TSM_WORDS_PER_LOCK; i++) {
            shared_lock_cleanup(&((currentSegment->locks + i)->lock));
        }
        free(currentSegment->locks);
        free(currentSegment);
        currentSegment = nextSegment;
    }
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
    newTransaction->version = region->globalVersion;
    newTransaction->readSetTail = NULL;
    newTransaction->writeSetHead = NULL;
    newTransaction->writeSetTail = NULL;
    newTransaction->next = NULL;
    newTransaction->isReadOnly = is_ro;
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
    return newTransaction->id;
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
**/
bool tm_end(shared_t shared, tx_t tx) {
    Region* region = (Region *) shared;
    transaction* transaction = getTransaction(region, tx);
    // The commit check will be different depending on whether the transaction is read-only or not.
    read_set_node * currentReadSetNode = transaction->readSetHead;
    while(currentReadSetNode != NULL){
        if(currentReadSetNode->version != getVersion(currentReadSetNode->segment,region,currentReadSetNode->offset)){
            // If the version of the lock node is different from the version of the read set node, then
            // the transaction failed. As this transaction was working with old data, we need to abort it.
            return false;
        }
            // We have to also check if the lock of this read set node is currently held by another transaction.
        else{
            shared_lock_t lock = getLock(currentReadSetNode->segment,currentReadSetNode->offset);
            if(shared_lock_is_locked(&lock)){
                // If the lock is held by another transaction, then we need to abort this transaction.
                return false;
            }
        }
        currentReadSetNode = currentReadSetNode->next;
    }
    if(!transaction->isReadOnly){
        // If the transaction is not read-only, then the write set also should be checked
        // we first need to acquire the locks in the write set.
        if(acquireLocks(transaction)){
            //TODO
        }
        else{
            // If we cannot acquire the locks, then we abort the transaction.
            return false;
        }
    }
    // If we reach this point, then the transaction succeeded.
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
    if (unlikely(!transaction)) { // No transaction could be found with the given id
        return false;
    }
    // Check if size is a positive multiple of the alignment.
    if (unlikely(size % region->align != 0 || size <= 0)) {
        clearSets(transaction);
        return false;
    }
    // First we need to find which segment the memory location we are trying to read from is in.
    segment_node* segment = findSegment(region, source);
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
    // One read operation can only read one word.
    // So we need to check if the memory location we are trying to read from is aligned.
    if ((size_t) source % region->align != 0) {
        // If no, then the transaction is aborted.
        // This is because the memory location we are trying to read from is not aligned.
        // Before aborting clear the read and write sets of the transaction.
        clearSets(transaction);
        return false;
    }
    // We are required to read size amount of bytes from the given memory location.
    // Given that we have our word size equal to the alignment, we will have size / alignment words to read.
    size_t currentWord = 0;
    while(currentWord < size / region->align){
        // Check if the memory location we are trying to write to already exists in the write set.
        void* currentTarget = (void*) target + currentWord * region->align;
        void* currentSource = (void*) source + currentWord * region->align;
        // Check if the transaction is read-only.
        if (!transaction->isReadOnly) {
            // The current transaction is NOT read-only.
            // Then we first check if the memory location we are trying to read from already exists in the write set.
            write_set_node *writeSetNode = checkWriteSet(region, transaction, currentSource);
            if (writeSetNode != NULL) {
                // If yes, then we can just copy the value from the write set as this is the value we have previously written.
                memcpy(currentTarget, writeSetNode->newValue, region->align);
                continue;
            }
        }
        // We get here if the transaction is read-only or the memory location
        // we are trying to read from does not exist in the write set.
        // Then we check if the memory location we are trying to read from already exists in the read set.
        read_set_node * readSetNode = checkReadSet(region, transaction, currentSource);
        if(readSetNode != NULL){
            // If yes, then we can just copy the old value we have read previously.
            memcpy(currentTarget, readSetNode->value, region->align);
            continue;
        }
        // If no, then we need to read from the shared memory as it is our very first time reading from this memory location.
        // First we need to find the offset of the memory location we are trying to read from.
        size_t offset = (size_t) ((void*) currentSource - segment->freeSpace) / region->align;
        // Check if the memory location is unlocked
        shared_lock_t lock = getLock(segment, offset);
        if (shared_lock_is_locked(&lock)) {
            // If no, then the transaction is aborted.
            // This is because the memory location we are trying to read from is being written to by another transaction.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // Then we need to get the version number of the word we are trying to write.
        uint64_t version = getVersion(segment, region, offset);
        // Check if this version is greater than the transaction's version.
        if (version > transaction->version) {
            // If yes, then the transaction is aborted.
            // This is because the memory location we are trying to read from, has been written by another transaction.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // Then we need to read the value of the word.
        // because we are reading one word at a time, size is always equal to the alignment.
        void* value = aligned_alloc(region->align,region->align);
        if(value == NULL){
            // If the malloc fails, then we return false.
            clearSets(transaction);
            return false;
        }
        memcpy(value, currentSource, region->align); // we save the value we read in case we read it again.
        // Then we need to add the read set node to the read set.
        addReadSet(transaction, segment, offset, version, value);
        // Then we need to copy the value to the target.
        memcpy(currentTarget, value, region->align);
        currentWord++;
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
        // This is because the size is not a positive multiple of the alignment.
        // Before aborting clear the whole read and write set of the transaction.
        clearSets(transaction);
        return false;
    }
    // First we need to find which segment the memory location we are trying to write to is in.
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
    // One write operation can only write to one word.
    // So we need to check if the memory location we are trying to write to is aligned.
    if ((size_t) target % region->align != 0) {
        // If no, then the transaction is aborted.
        // This is because the memory location we are trying to write to is not aligned.
        // Before aborting clear the read and write sets of the transaction.
        clearSets(transaction);
        return false;
    }
    // We are required to write size amount of bytes to the memory location.
    // Given that we have our word size equal to the alignment, we will have size / alignment words to write.
    size_t currentWord = 0;
    while(currentWord < size / region->align){
        void* currentTarget = (void*) target + currentWord * region->align;
        void* currentSource = (void*) source + currentWord * region->align;
        // Check if the memory location we are trying to write to already exists in the write set.
        write_set_node* writeSetNode = checkWriteSet(region, transaction, currentTarget);
        if(writeSetNode != NULL){
            // If yes, then we can just copy the new value to the write set.
            memcpy(writeSetNode->newValue, currentSource, region->align);
            continue;
        }
        // If no, then we need to read from the shared memory.
        // First we need to find the offset of the memory location we are trying to write to.
        size_t offset = (size_t) ((void*) currentTarget - segment->freeSpace) / region->align;
        // Check if the memory location is unlocked
        shared_lock_t lock = getLock(segment, offset);
        if (shared_lock_is_locked(&lock)) {
            // If no, then the transaction is aborted.
            // This is because the memory location we are trying to write to is being written to by another transaction.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // Then we need to get the version number of the word we are trying to write.
        uint64_t version = getVersion(segment, region, offset);
        // Check if this version is greater than the transaction's version.
        if (version > transaction->version) {
            // If yes, then the transaction is aborted.
            // This is because the memory location we are trying to write to has been written by another transaction.
            // Before aborting clear the whole read and write set of the transaction.
            clearSets(transaction);
            return false;
        }
        // Then we need to read the old value of the word we are trying to write.
        // because we are writing one word at a time, size is always equal to the alignment.
        void* value = aligned_alloc(region->align,region->align);
        if(value == NULL){
            // If the malloc fails, then we return false.
            clearSets(transaction);
            return false;
        }
        // This way we have saved the old value of the word.
        memcpy(value, currentTarget, region->align);
        // Now we also need to write the possible new value of the word
        void* newValue = aligned_alloc(region->align,region->align);
        if(newValue == NULL){
            // If the malloc fails, then we return false.
            clearSets(transaction);
            return false;
        }
        memcpy(newValue, currentSource, region->align);
        // Then we need to add the write set node to the transaction.
        addWriteSet(transaction, segment, offset, version, value, newValue);
        currentWord++;
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
    // First create a new segment node of given size and alignment.
    segment_node* newSegment = (segment_node *) malloc(sizeof(segment_node));
    if (unlikely(!newSegment)) {
        return abort_alloc;
    }
    Region* region = (Region *) shared;
    // We allocate the shared memory buffer such that its words are correctly aligned.
    if (posix_memalign(&(newSegment->freeSpace), region->align, size) != 0) {
        free(newSegment);
        return nomem_alloc;
    }
    // Initialize locks in the segment, as each lock is associated with TSM_WORDS_PER_LOCK words, we need to initialize
    // size / align / TSM_WORDS_PER_LOCK locks.
    // But as this can give us a decimal number, we need to round it up to the next integer.
    newSegment->lock_size = (int) ceil((double) size / region->align / TSM_WORDS_PER_LOCK);
    newSegment->locks = (lock_node *) malloc(sizeof(lock_node) * newSegment->lock_size);
    if(unlikely(!newSegment->locks)) {
        free(newSegment->freeSpace);
        free(newSegment);
        return nomem_alloc;
    }
    for (int i = 0; i < newSegment->lock_size; i++) {
        shared_lock_init(&((newSegment->locks + i)->lock));
        newSegment->locks[i].version = region->globalVersion;
    }
    // Add this segment to the list of segments of the region.
    newSegment->prev = region->allocTail; // The prev pointer of this segment points to the last segment of the region.
    region->allocTail->next = newSegment; // The next pointer of the last segment of the region points to this segment.
    newSegment->next = NULL; // The next pointer of this segment is NULL. As it is currently the latest segment of the region.

    memset(newSegment->freeSpace, 0, size);
    newSegment->size  = size;
    newSegment->id    = region->allocTail->id + 1;
    region->allocTail = newSegment; // Latest segment of the region is updated to be this segment.
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
    segment_node* currentSegment = region->allocTail;
    while (currentSegment != NULL) {
        if (currentSegment->freeSpace == target) {
            break;
        }
        currentSegment = currentSegment->prev;
    }
    if (currentSegment == NULL) {
        return false;
    }
    // If the segment's id is 0 then it is the first segment allocated thus we cannot free it.
    if (currentSegment->id == 0) {
        return false;
    }
    // Remove the segment node from the list of segments of the region.
    if (currentSegment->prev != NULL) {
        currentSegment->prev->next = currentSegment->next;
    }
    if (currentSegment->next != NULL) {
        currentSegment->next->prev = currentSegment->prev;
    }
    // Free the memory buffer.
    free(currentSegment->freeSpace);
    // Cleanup all locks
    for (int i = 0; i < currentSegment->size / region->align / TSM_WORDS_PER_LOCK; i++) {
        shared_lock_cleanup(&((currentSegment->locks + i)->lock));
    }
    free(currentSegment->locks);
    free(currentSegment);
    return true;
}
