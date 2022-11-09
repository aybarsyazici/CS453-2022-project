//
// Created by Aybars Yazici on 6.11.2022.
//

#include <printf.h>
#include "tm_helpers.h"

void addReadSet(transaction *pTransaction, segment_node *pSegment, size_t offset) {
    read_set_node * newReadSet = (read_set_node *) malloc(sizeof(read_set_node)); // Create a new readSet node
    newReadSet->segment = pSegment; // Which segment we read from
    newReadSet->offset = offset; // Set the offset(how many words away from the start of the segment)
    newReadSet->next = NULL; // The next is NULL as this is the newest node
    if (pTransaction->readSetHead == NULL) { // If the head is NULL, then this is the first node
        pTransaction->readSetHead = newReadSet;
        pTransaction->readSetTail = newReadSet;
        pTransaction->readSetSize = 1;
    }
    else { // If the head is not NULL, then we have to add the node to the end of the list
        pTransaction->readSetTail->next = newReadSet;
        pTransaction->readSetTail = newReadSet;
        pTransaction->readSetSize++;
    }
}

read_set_node *checkReadSet(Region *pRegion, transaction *pTransaction, void *address) {
    read_set_node *currentReadSet = pTransaction->readSetHead;
    while (currentReadSet != NULL) {
        // Each read set node contains which segment it read from and the offset from the start of the segment
        // Note that the offset is kept in terms of alignments(i.e. words), not bytes
        // So to check if this current read set node read from the address we are looking for,
        // we need to add the offset to the start of the segment and check if it is equal to the address.
        if (currentReadSet->segment->freeSpace + currentReadSet->offset * pRegion->align == address) {
            return currentReadSet;
        }
        currentReadSet = currentReadSet->next;
    }
    return NULL;
}

segment_node *findSegment(Region *pRegion, void *address) {
    segment_node *currentSegment = pRegion->allocHead;
    while (currentSegment != NULL) {
        if (currentSegment->freeSpace <= address && address < currentSegment->freeSpace + currentSegment->size) {
            return currentSegment;
        }
        currentSegment = currentSegment->next;
    }
    return NULL;
}

void addWriteSet(transaction *pTransaction, segment_node *pSegment, size_t offset, void *value) {
    write_set_node *newWriteSet = (write_set_node *) malloc(sizeof(write_set_node));
    newWriteSet->segment = pSegment;
    newWriteSet->offset = offset;
    newWriteSet->value = value;
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

write_set_node *checkWriteSet(Region *pRegion, transaction *pTransaction, void *address) {
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    while (currentWriteSet != NULL) {
        // Each write set node contains which segment it wrote to and the offset from the start of the segment
        // Note that the offset is kept in terms of alignments(i.e. words), not bytes
        // So to check if this current write set node wrote to the address we are looking for,
        // we need to add the offset to the start of the segment and check if it is equal to the address.
        if (currentWriteSet->segment->freeSpace + currentWriteSet->offset * pRegion->align == address) {
            return currentWriteSet;
        }
        currentWriteSet = currentWriteSet->next;
    }
    return NULL;
}

lock_t* getLock(segment_node *pSegment, size_t offset) {
    // Remember that each segment has one lock per TSM_WORDS_PER_LOCK words.
    // the lock at index 'i' protects the words at indices i * TSM_WORDS_PER_LOCK to (i + 1) * TSM_WORDS_PER_LOCK - 1 (inclusive.)
    // But imagine the segment has 12 words and TSM_WORDS_PER_LOCK is 5
    // Then the segment will have 3 locks, and the lock at index 0 will protect the words at indices 0 to 4 (inclusive)
    // The lock at index 1 will protect the words at indices 5 to 9 (inclusive)
    // The lock at index 2 will protect the words at indices 10 to 11 (inclusive)
    return &((pSegment->locks + offset / TSM_WORDS_PER_LOCK)->lock);
    // Notice how in our previous example all the indexes from 0 to 4 are divided by 5 and the result is 0
    // For indexes 5 to 9, the result is 1, and for indexes 10 to 11, the result is 2, exactly as we want.
}

atomic_uint* getVersion(segment_node *pSegment, size_t offset) {
    // Please take a look at getLock() to understand why we are doing this division to get the correct index.
    return &((pSegment->locks + offset / TSM_WORDS_PER_LOCK)->version);
}

lock_node* getLockNode(segment_node *pSegment, size_t offset) {
    // Please take a look at getLock() to understand why we are doing this division to get the correct index.
    return (pSegment->locks + offset / TSM_WORDS_PER_LOCK);
}

bool clearSets(transaction *pTransaction) {
    // First clear the write set
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    write_set_node *nextWriteSet;
    while (currentWriteSet != NULL) {
        nextWriteSet = currentWriteSet->next;
        free(currentWriteSet->value);
        free(currentWriteSet);
        currentWriteSet = nextWriteSet;
    }
    pTransaction->writeSetHead = NULL;
    pTransaction->writeSetTail = NULL;
    pTransaction->writeSetSize = 0;
    // Now clear the read set
    read_set_node *currentReadSet = pTransaction->readSetHead;
    read_set_node *nextReadSet;
    while (currentReadSet != NULL) {
        nextReadSet = currentReadSet->next;
        free(currentReadSet);
        currentReadSet = nextReadSet;
    }
    pTransaction->readSetHead = NULL;
    pTransaction->readSetTail = NULL;
    pTransaction->readSetSize = 0;
    free(pTransaction);
    return true;
}

transaction *getTransaction(Region *pRegion, tx_t tx) {
    return (transaction *)tx;
}

lock_t** getLocks(transaction *pTransaction) {
    lock_t** locks = (lock_t**)malloc(sizeof(lock_t*) * pTransaction->writeSetSize);
    write_set_node *current = pTransaction->writeSetHead;
    for (int i = 0; i < pTransaction->writeSetSize; i++) {
        locks[i] = getLock(current->segment, current->offset);
        current = current->next;
    }
    // sort locks by increasing order of the address
    qsort(locks, pTransaction->writeSetSize, sizeof(lock_t*), compareLocks);
    return locks;
}

bool acquireLocks(lock_t** locks, int size, size_t transactionId) {
    for (int i = 0; i < size; i++) {
        if (!lock_acquire(locks[i], transactionId)) {
            // failed to acquire a lock, release all the locks it has acquired so far
            for (int j = 0; j < i; j++) {
                lock_release(locks[j],transactionId);
            }
            return false;
        }
    }
    return true;
}

void releaseLocks(lock_t** locks, size_t size, size_t transactionId) {
    for (int i = 0; i < size; i++) {
        lock_release(locks[i], transactionId);
    }
}

int compareLocks(const void *a, const void *b) {
    lock_t** lock1 = (lock_t **)a;
    lock_t** lock2 = (lock_t **)b;
    if(&((*lock1)->mutex) < &((*lock2)->mutex)){
        return -1;
    }
    else if(&((*lock1)->mutex) > &((*lock2)->mutex)){
        return 1;
    }
    else{
        return 0;
    }
}

void releaseLocks_naive(transaction *pTransaction) {
    // Iterate over the write set
    write_set_node *pWriteSetNode = pTransaction->writeSetHead;
    while (pWriteSetNode != NULL) {
        // Get the lock for the current write set node
        lock_node* lockNode = getLockNode(pWriteSetNode->segment, pWriteSetNode->offset);
        // Release the lock
        lock_release(&lockNode->lock, pTransaction->id);
        // Move to the next write set node
        pWriteSetNode = pWriteSetNode->next;
    }
}

bool acquireLocks_naive(transaction *pTransaction) {
    // Iterate over the write set
    write_set_node* pWriteSetNode = pTransaction->writeSetHead;
    while(pWriteSetNode != NULL){
        // Get the lock for the current write set node
        lock_node* lockNode = getLockNode(pWriteSetNode->segment, pWriteSetNode->offset);
        // Try to acquire the lock
        if(!lock_acquire(&lockNode->lock, pTransaction->id)){
            // If we failed to acquire the lock, release all the locks we acquired so far
            releaseLocks_naive(pTransaction);
            return false;
        }
        // Move to the next write set node
        pWriteSetNode = pWriteSetNode->next;
    }
    return true;
}
