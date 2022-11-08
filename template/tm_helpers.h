//
// Created by Aybars Yazici on 6.11.2022.
//

#ifndef CS453_2022_PROJECT_TM_HELPERS_H
#define CS453_2022_PROJECT_TM_HELPERS_H
#include "tsm_types.h"
#include "macros.h"
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
 * @param version The current version of the word that we are reading.
 * @param value The value of the word that we are reading at read time
 **/
void addReadSet(transaction* pTransaction, segment_node* pSegment, size_t offset, uint64_t version, void* value);

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
 * @param version The current version of the word that we are writing.
 * @param value The value of the word that we are writing.
 * @param newValue The new value of the word that we are writing.
 **/
void addWriteSet(transaction* pTransaction,
                 segment_node* pSegment,
                 size_t offset,
                 uint64_t version,
                 void* value,
                 void* newValue);

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
lock_t getLock(segment_node* pSegment, size_t offset);

/** This function is used to get the version number of a word in a given segment.
 * @param segment The segment to get the version number from.
 * @param offset The number of alignments from the start of the segment.
 * @return The version number of the word.
 **/
uint64_t getVersion(segment_node* pSegment, size_t offset);

/** This function is used to get the lock node of a word in a given segment.
 * @param segment The segment to get the version number from.
 * @param offset The number of alignments from the start of the segment.
 * @return The version number of the word.
 **/
lock_node* getLockNode(segment_node* pSegment, size_t offset);

/** This function sorts the write set of the given transaction
 * by increasing order of the address of locks it needs to acquire
 * @param pTransaction pointer to the transaction
 * @return void
**/

void sortWriteSet(transaction *pTransaction);

/** This function is used to release all the locks of the given transaction
 * We once again assume the write set is already sorted by increasing order of the address of locks.
 * @param pTransaction The transaction to release the locks of.
 * @return void
 **/
void releaseLocks(transaction* pTransaction);


/** This function is used to acquire the locks associated with the write set of a given transaction.
 * We assume that the given transaction's write set
 * is sorted by increasing order of the address of locks it needs to acquire
 * @param transaction The transaction to acquire the locks for.
 * @return true if the locks were acquired successfully, or false if the locks couldn't be acquired.
 **/
bool acquireLocks(transaction* pTransaction);

/** This function clears the read and write sets of the given transaction.
 * @param transaction The transaction to clear the read and write sets of.
 * @return true if the read and write sets were cleared successfully, or false if they couldn't be cleared.
 **/
bool clearSets(transaction* pTransaction);

/** This function is used to increment the version of all lock nodes corresponding to the write set of a given transaction.
 * We assume that the given transaction's write set
 * is sorted by increasing order of the address of locks it needs to acquire
 * @param transaction The transaction to increment the version of.
 * @param shared The region the transaction belongs to
 * @return void
 **/
void incrementVersion(transaction* pTransaction, shared_t shared);

/** This function is used to destroy given transaction. It releases all the locks associated with the transaction,
 * clears the read and write sets, and frees the transaction.
 * @param transaction The transaction to destroy.
 * @return void
**/
void destroyTransaction(transaction* pTransaction);

/**
 ***************************************************************************************
 * END OF HELPER FUNCTIONS
 ***************************************************************************************
 **/



