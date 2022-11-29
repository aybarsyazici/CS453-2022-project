//
// Created by Aybars Yazici on 6.11.2022.
//

#ifndef CS453_2022_PROJECT_TM_HELPERS_H
#define CS453_2022_PROJECT_TM_HELPERS_H
#include "tsm_types.h"
#include "macros.h"
#include "bloom.h"
#include <stdlib.h>
#endif //CS453_2022_PROJECT_TM_HELPERS_H


/****************************************************************************************/
/**                                                                                    **/
/**  START OF HELPER FUNCTION DECLARATIONS                                             **/
/**                                                                                    **/
/****************************************************************************************/

/** This function is used to get the transaction with the given id from the region.
 * @param region The region to get the transaction from.
 * @param id The id of the transaction to get.
 * @return The transaction with the given id or NULL if no such transaction exists.
 **/
transaction* getTransaction(Region *pRegion, tx_t tx);

/** This function is used to add a new readSet node to the given transaction.
 * @param transaction The transaction to add the readSet node to.
 * @param segment The memory segment that we are reading from.
 * @param offset The number of alignments from the start of the segment.
 **/
void addReadSet(transaction* pTransaction, segment_node* pSegment, size_t offset);

/** This function is used to check if a given address is inside the read set of a given transaction.
 * @param shared The shared memory region that the address belongs to.
 * @param transaction The transaction to check the read set of.
 * @param address The address to check.
 * @return The read set node that contains the address, or NULL if the address is not in the read set.
 **/
read_set_node* checkReadSet(Region* pRegion, transaction* pTransaction, void* address);

/** This function is used to find the segment that contains the given address.
 * @param shared The shared memory region that the address belongs to.
 * @param address The address to find the segment of.
 * @return The segment that contains the address, or NULL if the address is not in any segment.
 **/
segment_node* findSegment(Region* pRegion, void* address);

/** This function is used to add a new writeSet node to the given transaction.
 * @param transaction The transaction to add the writeSet node to.
 * @param segment The memory segment that we are writing to.
 * @param offset The number of alignments from the start of the segment.
 * @param value The new value of the word that we are writing.
 * @param align The alignment/size of the word that we are writing.
 **/
void addWriteSet(transaction* pTransaction,
                 segment_node* pSegment,
                 size_t offset,
                 void* value,
                 int align
                 );

/** This function is used to check if a given address is inside the write set of a given transaction.
 * @param shared The shared memory region that the address belongs to.
 * @param transaction The transaction to check the write set of.
 * @param address The address to check.
 * @return The write set node that contains the address, or NULL if the address is not in the write set.
 **/
write_set_node* checkWriteSet(Region* pRegion, transaction* pTransaction, void* address);

/** This function is used to get the lock associated with a memory location in a given segment
 * @param segment The segment to get the lock from.
 * @param offset The number of alignments from the start of the segment.
 * @return The lock associated with the memory location.
 **/
lock_t* getLock(segment_node* pSegment, size_t offset);

/** This function is used to get the version number of a word in a given segment.
 * @param segment The segment to get the version number from.
 * @param offset The number of alignments from the start of the segment.
 * @return The version number of the word.
 **/
atomic_ulong* getVersion(segment_node* pSegment, size_t offset);

/** This function is used to get the lock node of a word in a given segment.
 * @param segment The segment to get the version number from.
 * @param offset The number of alignments from the start of the segment.
 * @return The version number of the word.
 **/
lock_node* getLockNode(segment_node* pSegment, size_t offset);

/** This function naively releases the locks for the transaction given
 * @param transaction The transaction to release the locks for.
 * @return void
 * */
void releaseLocks_naive(transaction* pTransaction);

/** This function naively get's the locks for the transaction given
 * @param transaction The transaction to get the locks for.
 * @return true if the locks were acquired, false otherwise.
**/
bool acquireLocks_naive(transaction* pTransaction);

/** This function is used to release all the locks in the given array of locks
 * @param locks The array of locks to release.
 * @param size The size of the array of locks.
 * @param transactionId The id of the transaction that is releasing the locks.
 * @return void
 **/
void releaseLocks(lock_t** locks, size_t size, size_t transactionId);

int compareLocks(const void *a, const void *b);

/** This function given a transaction returns the array of locks it needs to acquire
 * the array is sorted by increasing order of the address of locks
 * @param pTransaction pointer to the transaction
 * @return array of locks
**/
lock_t** getLocks(transaction *pTransaction);


/** This function given an array of lock nodes, tries to acquire all the locks in the array
 * If failed to acquire a lock, it releases all the locks it has acquired so far
 * @param locks array of locks
 * @param size size of the array
 * @param transactionId id of the transaction
 * @return true if all locks are acquired, false otherwise
 **/
bool acquireLocks(lock_t** locks, int size, size_t transactionId);


/** This function clears the read and write sets of the given transaction.
 * @param transaction The transaction to clear the read and write sets of.
 * @return true if the read and write sets were cleared successfully, or false if they couldn't be cleared.
 **/
bool clearSets(transaction* pTransaction);

/**
 ***************************************************************************************
 * END OF HELPER FUNCTIONS
 ***************************************************************************************
 **/



