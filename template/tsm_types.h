//
// Created by Aybars Yazici on 5.11.2022.
//

#ifndef CS453_2022_PROJECT_TSM_TYPES_H
#define CS453_2022_PROJECT_TSM_TYPES_H

#include <stdatomic.h>
#include "tm.h"
#include "lock.h"
#include "bloom.h"

#define TSM_WORDS_PER_LOCK 1
#define TSM_ARRAY_SIZE 65536

typedef struct lock_node {
    lock_t lock;
    atomic_ulong version;
} lock_node;

typedef struct segment_node {
    atomic_ulong allocator;
    atomic_bool accessible;
    atomic_bool deleted;
    shared_t freeSpace; // Actual free space in the segment
    shared_t fakeSpace; // Fake space to be returned to the user where he can write or read concurrently
    unsigned long size; // Size of the freeSpace in bytes
    unsigned long align; // Alignment of the freeSpace in bytes
    unsigned int id; // Id of the current segment
    // Each segment needs to keep a version number for each word in the segment
    // The size of a word is decided by the user when he creates the shared memory region
    // word size will be equal to the alignment size of the shared memory region
    // The size of the array is equal to the number of words in the segment divided by TSM_WORDS_PER_LOCK
    // Each lock is used to protect TSM_WORDS_PER_LOCK words in the segment
    // The lock at index i protects the words at indices i * TSM_WORDS_PER_LOCK to (i + 1) * TSM_WORDS_PER_LOCK - 1 (inclusive.)
    // If the number of words in the segment is not a multiple of TSM_WORDS_PER_LOCK, the last lock protects the remaining words
    unsigned long lock_size; // Size of the lock array
    lock_node* locks; // Array of locks
} segment_node;

// Read set node to be used in a transaction
// Each node contains the info of which shared region it read from
// And the word it read, we keep this info as how many alignments we read from the start of the shared region
// finally the version at the time of the reading operation
typedef struct read_set_node {
    struct read_set_node* next; // Pointer to the next read set node
    segment_node* segment; // Pointer to the segment that the read set node is reading from
    unsigned long offset; // Word that the read set node is reading from, this is the index of the word in the segment
    // (i.e. how many alignments we read from the start of the segment)
} read_set_node;

// Write set node to be used in a transaction
// Each node contains the info of which shared region it wrote to
// And the word it wrote, we keep this info as how many alignments we wrote to the start of the shared region
// Also the potential new value of the word, this new value is not written to the shared region yet, it will be written at commit time
// finally the version at the time of the writing operation
typedef struct write_set_node {
    struct write_set_node* next; // Pointer to the next write set node
    segment_node* segment; // Pointer to the segment that the write set node is writing to
    unsigned long offset; // Word that the write set node is writing to, this is the index of the word in the segment
    // (i.e. how many alignments we wrote to the start of the segment)
    shared_t value; // new value of the word at the time of the writing operation
    uint64_t address;
} write_set_node;

typedef struct segment_array {
    segment_node** elements; // Array of pointers to segments
    atomic_bool* locks; // Array of locks for each segment
} segment_array;

typedef struct segment_ll{
    segment_node* segmentNode;
    struct segment_ll* next;
}segment_ll;

typedef struct Region {
    void* start;         // Start of the shared memory region (i.e., of the non-deallocable memory segment)
    segment_array segments; // Array of pointers to segments
    unsigned long align;       // Size of a word in the shared memory region (in bytes)
    atomic_ulong globalVersion; // Global version of the shared memory region
    atomic_ulong latestTransactionId; // Latest transaction id
    atomic_ulong txIdOnLatestFree; // Transaction id on latest free
    atomic_ulong finishedTxCount; // Number of finished transactions
    atomic_ulong finishedTxCountOnLatestFree; // Number of finished transactions on latest free
} Region;

// Transaction declaration to implement Transactional Locking II.
// A transaction can be a read only transaction or a read write transaction.
// Read only transactions only keep a read set and read write transactions keep a read and write set.
// Read and write sets are implemented as linked lists of read and write set nodes.
// A transaction is identified by a unique transaction id.
// A transaction can be aborted or committed.
typedef struct transaction{
    unsigned long id; // Unique transaction id
    Region* region;
    unsigned long version; // Global version at the time of the transaction start
    read_set_node* readSetHead; // Pointer pointing to the first of the read set nodes
    read_set_node* readSetTail; // Pointer pointing to the last of the read set nodes
    write_set_node* writeSetHead; // Pointer pointing to the first of the write set nodes
    write_set_node* writeSetTail; // Pointer pointing to the last of the write set nodes
    segment_ll* allocListHead; // Pointer pointing to the first of the allocated segments
    segment_ll* allocListTail; // Pointer pointing to the last of the allocated segments
    segment_ll* freeListHead; // Pointer pointing to the first of the free segments
    segment_ll* freeListTail; // Pointer pointing to the last of the free segments
    int readSetSize; // Size of the read set
    int writeSetSize; // Size of the write set
    bool isReadOnly; // Boolean to check if the transaction is read only or not
} transaction;


#endif //CS453_2022_PROJECT_TSM_TYPES_H
