//
// Created by Aybars Yazici on 6.11.2022.
//

#include <printf.h>
#include "tm_helpers.h"

void addReadSet(transaction *pTransaction, segment_node *pSegment, unsigned long offset) {
    read_set_node *newReadSet = pTransaction->readSetTail;
    newReadSet->segment = pSegment;
    newReadSet->offset = offset;
    pTransaction->readSetSize++;
    if(pTransaction->readSetSize % SET_START_SIZE != 0){
        pTransaction->readSetTail = pTransaction->readSetTail+1;
        newReadSet->next = pTransaction->readSetTail;
    }
    else{
        pTransaction->readSetTail->next = (read_set_node *) malloc(sizeof(read_set_node)*SET_START_SIZE);
        pTransaction->readSetTail = pTransaction->readSetTail->next;
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

segment_node *findSegment(Region *pRegion, void *address, transaction *pTransaction) {
    int index = (int) ((unsigned long) address >> 48);
    segment_node* toReturn = pRegion->segments.elements[index];
    if(toReturn == NULL) {
        // printf("Segment not found, address: %p",address);
        return NULL;
    };
    if(toReturn->accessible == 0 && pTransaction->id != toReturn->allocator){
        // printf("Segment %d, segmentFakeSpace %p\n",toReturn->id, toReturn->fakeSpace);
        // printf("PROBLEM!\n");
        return NULL;
    }
    if(toReturn->deleted) {
        // printf("Segment %d is deleted\n",toReturn->id);
        return NULL;
    }
    return toReturn;
}

void addWriteSet(transaction *pTransaction, segment_node *pSegment, unsigned long offset, void *value, unsigned long align) {
    write_set_node *newWriteSet = pTransaction->writeSetTail;
    newWriteSet->segment = pSegment;
    newWriteSet->offset = offset;
    newWriteSet->value = value;
    newWriteSet->address = (uint64_t) &(*(pSegment->freeSpace + offset * align));
    pTransaction->writeSetSize++;
    if(pTransaction->writeSetSize % SET_START_SIZE != 0){
        pTransaction->writeSetTail = pTransaction->writeSetTail+1;
        newWriteSet->next = pTransaction->writeSetTail;
    }
    else{
        // printf("TX%lu: Write set is full %d SET_START_SIZE:%d, RESULT:%d \n",pTransaction->id, pTransaction->writeSetSize, SET_START_SIZE, pTransaction->writeSetSize % SET_START_SIZE);
        pTransaction->writeSetTail->next = (write_set_node *) malloc(sizeof(write_set_node)*SET_START_SIZE);
        pTransaction->writeSetTail = pTransaction->writeSetTail->next;
        // printf("TX%lu: Write set Extended %d\n",pTransaction->id, pTransaction->writeSetSize);
    }

    // Add to bloom filter
    // bloom_add(pTransaction->writeSetBloom, &newWriteSet->address, sizeof(shared_t));
    // Hash the address and add it to the hashmap
//    unsigned int index = murmurhash2(&newWriteSet->address, sizeof(uint64_t), 0);
//    write_set_node *current = pTransaction->hashMap[index % HASH_SIZE];
//    if(current != NULL){
//        while (current->hashMapNext != NULL) {
//            current = current->hashMapNext;
//        }
//        current->hashMapNext = newWriteSet;
//    }else {
//        pTransaction->hashMap[index % HASH_SIZE] = newWriteSet;
//    }

}

write_set_node *checkWriteSet(Region *pRegion, transaction *pTransaction, void *address) {
    // Check if address is in bloom filter
    // uint64_t toCheck = ((uint64_t)&(*address));
    // if (!bloom_check(pTransaction->writeSetBloom, &toCheck, sizeof(uint64_t))) {
        // return NULL;
    // }
//    uint64_t temp = (uint64_t) &(*address);
//    unsigned int index = murmurhash2(&temp, sizeof(uint64_t), 0);
//    write_set_node *current = pTransaction->hashMap[index % HASH_SIZE];
//    if(current == NULL){
//        return NULL;
//    } else {
//        while(current->address != (uint64_t) &(*address)){
//            current = current->hashMapNext;
//            if(current == NULL){
//                return NULL;
//            }
//        }
//        return current;
//    }
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    while (currentWriteSet != pTransaction->writeSetTail) {
        // Each write set node contains which segment it wrote to and the offset from the start of the segment
        // Note that the offset is kept in terms of alignments(i.e. words), not bytes
        // So to check if this current write set node wrote to the address we are looking for,
        // we need to add the offset to the start of the segment and check if it is equal to the address.
        if (currentWriteSet->address == (uint64_t)&(*address)) {
            return currentWriteSet;
        }
        currentWriteSet = currentWriteSet->next;
    }
    return NULL;
}

lock_t* getLock(segment_node *pSegment, unsigned long offset) {
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

atomic_ulong* getVersion(segment_node *pSegment, unsigned long offset) {
    // Please take a look at getLock() to understand why we are doing this division to get the correct index.
    return &((pSegment->locks + offset / TSM_WORDS_PER_LOCK)->version);
}

lock_node* getLockNode(segment_node *pSegment, unsigned long offset) {
    // Please take a look at getLock() to understand why we are doing this division to get the correct index.
    return (pSegment->locks + offset / TSM_WORDS_PER_LOCK);
}

bool clearSets(transaction *pTransaction, bool success) {
    // First clear the write set
    write_set_node *currentWriteSet = pTransaction->writeSetHead;
    write_set_node *nextWriteSet;
    int numberOfFreesRequired = (pTransaction->writeSetSize / SET_START_SIZE) + 1;
    for(int i = 0; i < numberOfFreesRequired; i++){
        nextWriteSet = (currentWriteSet+(SET_START_SIZE-1))->next;
        free(currentWriteSet);
        currentWriteSet = nextWriteSet;
    }
//    while (currentWriteSet != NULL) {
//        nextWriteSet = currentWriteSet->next;
//        if(currentWriteSet->segment == NULL){
//            // printf("WRITE SET SEGMENT NULL, SHOULD NOT HAPPEN\n");
//        }
//        free(currentWriteSet->value);
//        free(currentWriteSet);
//        currentWriteSet = nextWriteSet;
//    }
    pTransaction->writeSetHead = NULL;
    pTransaction->writeSetTail = NULL;
    pTransaction->writeSetSize = 0;
    // bloom_free(pTransaction->writeSetBloom);
    // free(pTransaction->writeSetBloom);
    // Now clear the read set
    read_set_node *currentReadSet = pTransaction->readSetHead;
    read_set_node *nextReadSet;
    numberOfFreesRequired = (pTransaction->readSetSize / SET_START_SIZE) + 1;
    for(int i = 0; i < numberOfFreesRequired; i++){
        nextReadSet = (currentReadSet+(SET_START_SIZE-1))->next;
        free(currentReadSet);
        currentReadSet = nextReadSet;
    }
//    while (currentReadSet != NULL) {
//        nextReadSet = currentReadSet->next;
//        if(currentReadSet->segment == NULL) {
//            // printf("READ SET SEGMENT NULL, SHOULD NOT HAPPEN\n");
//        }
//        free(currentReadSet);
//        currentReadSet = nextReadSet;
//    }
    pTransaction->readSetHead = NULL;
    pTransaction->readSetTail = NULL;
    pTransaction->readSetSize = 0;
    if(!success){
        // Free the alloc set
        segment_ll* allocSet = pTransaction->allocListHead;
        segment_ll* nextAllocSet;
        while (allocSet != NULL) {
            nextAllocSet = allocSet->next;
            pTransaction->region->segments.elements[allocSet->segmentNode->id] = NULL;
            free(allocSet->segmentNode->locks);
            free(allocSet->segmentNode->freeSpace);
            free(allocSet->segmentNode);
            free(allocSet);
            allocSet = nextAllocSet;
        }
    }
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
    // qsort(locks, pTransaction->writeSetSize, sizeof(lock_t*), compareLocks);
    return locks;
}

bool acquireLocks(lock_t** locks, unsigned long size, unsigned long transactionId) {
    for (int i = 0; i < size; i++) {
        if (!lock_acquire(locks[i], transactionId)) {
            // failed to acquire a lock, release all the locks it has acquired so far
            for (int j = 0; j < i; j++) {
                lock_release(locks[j], transactionId);
            }
            return false;
        }
    }
    return true;
}

void releaseLocks(lock_t** locks, unsigned long size, unsigned long transactionId) {
    for (int i = 0; i < size; i++) {
        lock_release(locks[i],transactionId);
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

void insertSegment(segment_array *array, segment_node *segment) {

    segment_node ** segments = array->elements;
    while(atomic_compare_exchange_strong((array->locks+segment->id), &(bool){false}, true) == false);
    if (segments[segment->id] == NULL) {
        segments[segment->id] = segment;
    } else {
        // printf("Segment %d already exists, should NOT HAPPEN\n", segment->id);
    }
    atomic_store((array->locks+segment->id), false);
}

int findEmptySegment(segment_array *array) {
    for(int i = 1; i < TSM_ARRAY_SIZE; i++){
        if(array->elements[i] == NULL){
            if(atomic_compare_exchange_strong((array->locks+i), &(bool){false}, true)){
                return i;
            }
        }
    }
    return -1;
}

bool freeSegments(transaction *pTransaction, Region* region) {
    // printf("T%lu: Free Called.\n",pTransaction->id);
    segment_ll* allocSet = pTransaction->allocListHead;
    segment_ll* nextAllocSet;
    while (allocSet != NULL) {
        nextAllocSet = allocSet->next;
        allocSet->segmentNode->deleted = true;
        // region->segments.elements[allocSet->segmentNode->id] = NULL;
        // free(allocSet->segmentNode->locks);
        // free(allocSet->segmentNode->freeSpace);
        // free(allocSet->segmentNode);
        free(allocSet);
        allocSet = nextAllocSet;
    }
    // printf("T%lu: Free finished.\n",pTransaction->id);
    return true;
}

void allocateSegments(transaction* pTransaction, Region* region){
    segment_ll* pAllocSet = pTransaction->allocListHead;
    // printf("T%lu: Alloc Called.\n",pTransaction->id);
    while(pAllocSet != NULL){
        if(pAllocSet->segmentNode != NULL){
            // printf("T%lu: Marking segment %d as accessible.\n", pTransaction->id, pAllocSet->segmentNode->id);
            pAllocSet->segmentNode->accessible = true;
            pAllocSet->segmentNode->allocator = 0;
        }
        segment_ll* next = pAllocSet->next;
        free(pAllocSet);
        pAllocSet = next;
    }
    // printf("T%lu: Alloc finished.\n",pTransaction->id);
}
