//
// Created by Aybars Yazici on 5.11.2022.
//

#ifndef CS453_2022_PROJECT_TSM_TYPES_H
#define CS453_2022_PROJECT_TSM_TYPES_H
#include "tm.h"
#include "lock.h"
#define TSM_WORDS_PER_LOCK 1

typedef struct lock_node {
    lock_t lock;
    uint64_t version;
} lock_node;

typedef struct segment_node {
    struct segment_node* prev; // Pointer to the previous segment
    struct segment_node* next; // Pointer to the next segment
    shared_t freeSpace; // Free space to be returned to the user where he can write or read concurrently
    size_t size; // Size of the freeSpace in bytes
    uint64_t id; // Id of the current segment
    // Each segment needs to keep a version number for each word in the segment
    // The size of a word is decided by the user when he creates the shared memory region
    // word size will be equal to the alignment size of the shared memory region
    // The size of the array is equal to the number of words in the segment divided by TSM_WORDS_PER_LOCK
    // Each lock is used to protect TSM_WORDS_PER_LOCK words in the segment
    // The lock at index i protects the words at indices i * TSM_WORDS_PER_LOCK to (i + 1) * TSM_WORDS_PER_LOCK - 1 (inclusive.)
    // If the number of words in the segment is not a multiple of TSM_WORDS_PER_LOCK, the last lock protects the remaining words
    size_t lock_size; // Size of the lock array
    lock_node* locks; // Array of locks
} segment_node;

// Read set node to be used in a transaction
// Each node contains the info of which shared region it read from
// And the word it read, we keep this info as how many alignments we read from the start of the shared region
// finally the version at the time of the reading operation
typedef struct read_set_node {
    struct read_set_node* next; // Pointer to the next read set node
    segment_node* segment; // Pointer to the segment that the read set node is reading from
    size_t offset; // Word that the read set node is reading from, this is the index of the word in the segment
    shared_t value; // Value that the read set node read from the segment
    // (i.e. how many alignments we read from the start of the segment)
    uint64_t version; // Version of the word at the time of the reading operation
} read_set_node;

// Write set node to be used in a transaction
// Each node contains the info of which shared region it wrote to
// And the word it wrote, we keep this info as how many alignments we wrote to the start of the shared region
// Also the potential new value of the word, this new value is not written to the shared region yet, it will be written at commit time
// finally the version at the time of the writing operation
typedef struct write_set_node {
    struct write_set_node* next; // Pointer to the next write set node
    segment_node* segment; // Pointer to the segment that the write set node is writing to
    size_t offset; // Word that the write set node is writing to, this is the index of the word in the segment
    // (i.e. how many alignments we wrote to the start of the segment)
    shared_t value; // Old value of the word at the time of the writing operation
    shared_t newValue; // Potential new value of the word, this new value is not written to the shared region yet, it will be written at commit time
    uint64_t version; // Version of the word at the time of the writing operation
} write_set_node;


// Transaction declaration to implement Transactional Locking II.
// A transaction can be a read only transaction or a read write transaction.
// Read only transactions only keep a read set and read write transactions keep a read and write set.
// Read and write sets are implemented as linked lists of read and write set nodes.
// A transaction is identified by a unique transaction id.
// A transaction can be aborted or committed.
typedef struct transaction{
    size_t id; // Unique transaction id
    uint64_t version; // Global version at the time of the transaction start
    read_set_node* readSetHead; // Pointer pointing to the first of the read set nodes
    read_set_node* readSetTail; // Pointer pointing to the last of the read set nodes
    write_set_node* writeSetHead; // Pointer pointing to the first of the write set nodes
    write_set_node* writeSetTail; // Pointer pointing to the last of the write set nodes
    uint64_t readSetSize; // Size of the read set
    uint64_t writeSetSize; // Size of the write set
    struct transaction* next; // Pointer to the next transaction
    struct transaction* prev; // Pointer to the previous transaction
    bool isReadOnly; // Boolean to check if the transaction is read only or not
} transaction;

typedef struct Region {
    void* start;         // Start of the shared memory region (i.e., of the non-deallocable memory segment)
    segment_node* allocHead; // Pointer pointing to the first of the shared memory segments dynamically allocated via tm_alloc within transactions
    segment_node* allocTail; // Pointer pointing to the last of the shared memory segments dynamically allocated via tm_alloc within transactions
    size_t align;       // Size of a word in the shared memory region (in bytes)
    transaction* transactions; // Linked list of transactions running on this shared memory region
    uint64_t globalVersion; // Global version of the shared memory region
    lock_t* globalLock; // Global lock to protect region
} Region;

#endif //CS453_2022_PROJECT_TSM_TYPES_H
