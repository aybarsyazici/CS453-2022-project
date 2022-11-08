//
// Created by Aybars Yazici on 6.11.2022.
//

#include <printf.h>
#include "tm_helpers.h"

void addReadSet(transaction *pTransaction, segment_node *pSegment, size_t offset, uint64_t version, void *value) {
    read_set_node * newReadSet = (read_set_node *) malloc(sizeof(read_set_node)); // Create a new readSet node
    newReadSet->segment = pSegment; // Which segment we read from
    newReadSet->offset = offset; // Set the offset(how many words away from the start of the segment)
    newReadSet->version = version; // The version at the time of reading
    newReadSet->value = value; // The value we read
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

void addWriteSet(transaction *pTransaction, segment_node *pSegment, size_t offset, uint64_t version, void *value,
                 void *newValue) {
    write_set_node *newWriteSet = (write_set_node *) malloc(sizeof(write_set_node));
    newWriteSet->segment = pSegment;
    newWriteSet->offset = offset;
    newWriteSet->version = version;
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

lock_t getLock(segment_node *pSegment, size_t offset) {
    // Remember that each segment has one lock per TSM_WORDS_PER_LOCK words.
    // the lock at index 'i' protects the words at indices i * TSM_WORDS_PER_LOCK to (i + 1) * TSM_WORDS_PER_LOCK - 1 (inclusive.)
    // But imagine the segment has 12 words and TSM_WORDS_PER_LOCK is 5
    // Then the segment will have 3 locks, and the lock at index 0 will protect the words at indices 0 to 4 (inclusive)
    // The lock at index 1 will protect the words at indices 5 to 9 (inclusive)
    // The lock at index 2 will protect the words at indices 10 to 11 (inclusive)
    return (pSegment->locks + offset / TSM_WORDS_PER_LOCK)->lock;
    // Notice how in our previous example all the indexes from 0 to 4 are divided by 5 and the result is 0
    // For indexes 5 to 9, the result is 1, and for indexes 10 to 11, the result is 2, exactly as we want.
}

uint64_t getVersion(segment_node *pSegment, size_t offset) {
    // Please take a look at getLock() to understand why we are doing this division to get the correct index.
    return (pSegment->locks + offset / TSM_WORDS_PER_LOCK)->version;
}

lock_node* getLockNode(segment_node *pSegment, size_t offset) {
    // Please take a look at getLock() to understand why we are doing this division to get the correct index.
    return (pSegment->locks + offset / TSM_WORDS_PER_LOCK);
}

void sortWriteSet(transaction *pTransaction) {
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    write_set_node *nextWriteSet;
    while (currentWriteSet != NULL) {
        nextWriteSet = currentWriteSet->next;
        while (nextWriteSet != NULL) {
            lock_t currentLock = getLock(currentWriteSet->segment, currentWriteSet->offset);
            lock_t nextLock = getLock(nextWriteSet->segment, nextWriteSet->offset);
            if (&currentLock > &nextLock) {
                // Swap the values
                segment_node *tempSegment = currentWriteSet->segment;
                size_t tempOffset = currentWriteSet->offset;
                uint64_t tempVersion = currentWriteSet->version;
                void *tempValue = currentWriteSet->value;
                void *tempNewValue = currentWriteSet->newValue;
                currentWriteSet->segment = nextWriteSet->segment;
                currentWriteSet->offset = nextWriteSet->offset;
                currentWriteSet->version = nextWriteSet->version;
                currentWriteSet->value = nextWriteSet->value;
                currentWriteSet->newValue = nextWriteSet->newValue;
                nextWriteSet->segment = tempSegment;
                nextWriteSet->offset = tempOffset;
                nextWriteSet->version = tempVersion;
                nextWriteSet->value = tempValue;
                nextWriteSet->newValue = tempNewValue;
            }
            nextWriteSet = nextWriteSet->next;
        }
        currentWriteSet = currentWriteSet->next;
    }
    printf("Write set sorted\n");
}

void releaseLocks(transaction *pTransaction) {
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    while (currentWriteSet != NULL) {
        lock_t lock = getLock(currentWriteSet->segment, currentWriteSet->offset);
        // Release the lock
        lock_release(&lock, pTransaction->id);
        currentWriteSet = currentWriteSet->next;
    }
}

bool acquireLocks(transaction *pTransaction) {
    // What we should watch out for is that multiple nodes may be trying to acquire the same lock.
    // This is because one lock may protect multiple words in the same segment.
    // So we need to make sure that we only acquire a lock once.
    // We do this by keeping the track of the last segment and offset we acquired a lock for.
    // If the current segment and offset are the same as the last one, we skip acquiring the lock.
    segment_node* lastSegment;
    size_t lastOffset;
    bool firstLock = true;

    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    while (currentWriteSet != NULL) {
        lock_t currentLock = getLock(currentWriteSet->segment, currentWriteSet->offset);
        // If this is the first lock we are trying to acquire, or if the current lock is different from the last lock we acquired
        if (firstLock
            || currentWriteSet->segment != lastSegment
            || (lastOffset / TSM_WORDS_PER_LOCK) != (currentWriteSet->offset / TSM_WORDS_PER_LOCK)) {
            // Try to acquire the lock
            if (!lock_acquire(&currentLock, pTransaction->id)) {
                // If we couldn't acquire the lock, release all the locks we acquired so far
                releaseLocks(pTransaction);
                return false;
            }
            // Update the last lock we acquired
            lastSegment = currentWriteSet->segment;
            lastOffset = currentWriteSet->offset;
            firstLock = false;
        }
        currentWriteSet = currentWriteSet->next;
    }
    return true;
}

bool clearSets(transaction *pTransaction) {
    // First clear the write set
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
    // Now clear the read set
    read_set_node *currentReadSet = pTransaction->readSetHead;
    read_set_node *nextReadSet;
    while (currentReadSet != NULL) {
        nextReadSet = currentReadSet->next;
        free(currentReadSet->value);
        free(currentReadSet);
        currentReadSet = nextReadSet;
    }
    pTransaction->readSetHead = NULL;
    pTransaction->readSetTail = NULL;
    pTransaction->readSetSize = 0;
    return true;
}

void incrementVersion(transaction *pTransaction, shared_t shared) {
    // What we should watch out for is that multiple nodes may be trying to increment the version of the same lock.
    // This is because one lock may protect multiple words in the same segment.
    // So we need to make sure that we only increment the version of a lock once.
    // We do this by keeping the track of the last segment and offset we incremented the version of.
    // If the current segment and offset are the same as the last one, we skip incrementing the version.
    segment_node* lastSegment;
    size_t lastOffset;
    bool firstLock = true;
    Region *region = (Region *)shared;
    if(unlikely(region == NULL)){
        return;
    }
    uint64_t globalVersion = region->globalVersion;
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    while (currentWriteSet != NULL) {
        lock_node* currentLockNode = getLockNode(currentWriteSet->segment, currentWriteSet->offset);
        // If this is the first lock we are trying to increment the version of, or if the current lock is different from the last lock we incremented the version of
        if (firstLock
            || currentWriteSet->segment != lastSegment
            || (lastOffset / TSM_WORDS_PER_LOCK) != (currentWriteSet->offset / TSM_WORDS_PER_LOCK)) {
            // Atomically set the new version equal to region globalVersion + 1
            __atomic_store_n(&currentLockNode->version, globalVersion + 1, __ATOMIC_SEQ_CST);
            // Update the last lock we incremented the version of
            lastSegment = currentWriteSet->segment;
            lastOffset = currentWriteSet->offset;
            firstLock = false;
        }
        currentWriteSet = currentWriteSet->next;
    }
}

void destroyTransaction(transaction *pTransaction) {
    // Release all the locks associated with the transaction
    releaseLocks(pTransaction);
    // Clear the read and write sets
    clearSets(pTransaction);
    // Free the transaction
    free(pTransaction);
}

transaction *getTransaction(Region *pRegion, tx_t tx) {
    transaction* currentTransaction = pRegion->transactions;
    while (currentTransaction != NULL) {
        if (currentTransaction->id == tx) {
            return currentTransaction;
        }
        currentTransaction = currentTransaction->prev;
    }
    return NULL;
}
