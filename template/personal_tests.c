//
// Created by Aybars Yazici on 5.11.2022.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tm.h"

typedef struct BankAccount{
    int balance;
    int id;
    char name[8];
}BankAccount;

void thread1(shared_t shared){
    // Start a transaction
    tx_t tx = tm_begin(shared, true);
    // Get all accounts from the shared memory region
    shared_t sharedRegion = tm_start(shared);
    size_t alignment = tm_align(shared);
    BankAccount* account1 = (BankAccount*) aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    tm_read(shared, tx, sharedRegion, sizeof(BankAccount),account1);
    BankAccount* account2 = (BankAccount*) aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    tm_read(shared, tx, sharedRegion + alignment, sizeof(BankAccount),account2);
    BankAccount* account3 = (BankAccount*) aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    tm_read(shared, tx, sharedRegion + 2*alignment, sizeof(BankAccount),account3);
    BankAccount* account4 = (BankAccount*) aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    tm_read(shared, tx, sharedRegion + 3*alignment, sizeof(BankAccount),account4);
    tm_end(shared, tx);

    // Print the balances and names of the accounts
    printf("Account 1: %d, %s\n", account1->balance, account1->name);
    printf("Account 2: %d, %s\n", account2->balance, account2->name);
    printf("Account 3: %d, %s\n", account3->balance, account3->name);
    printf("Account 4: %d, %s\n", account4->balance, account4->name);

}

int main(){
    // Print size of void pointer
    printf("Size of void pointer: %zu\n", sizeof(void*));
    // Print size of bank account
    printf("Size of bank account: %zu\n", sizeof(BankAccount));
    // Crete a shared memory region (the alignment must be a power of 2 and at least sizeof(void*))
    BankAccount* account1 = aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    account1->balance = 100;
    account1->id = 1;
    strcpy(account1->name, "Aybars");
    BankAccount* account2 = aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    account2->balance = 100;
    account2->id = 2;
    strcpy(account2->name, "Elif");
    BankAccount* account3 = aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    account3->balance = 100;
    account3->id = 3;
    strcpy(account3->name, "Vural");
    BankAccount* account4 = aligned_alloc(sizeof(BankAccount), sizeof(BankAccount));
    account4->balance = 100;
    account4->id = 3;
    strcpy(account4->name, "Nurdan");

    shared_t shared = tm_create(4*sizeof(BankAccount), sizeof(BankAccount));
    // Start a transaction
    tx_t tx = tm_begin(shared, false);
    // Write to the shared memory region
    shared_t sharedRegion = tm_start(shared);
    size_t alignment = tm_align(shared);
    if(tm_write(shared, tx, account1, sizeof(BankAccount), sharedRegion)){
        printf("Account 1 written to the shared memory region\n");
    }
    if(tm_write(shared, tx, account2, sizeof(BankAccount), sharedRegion + alignment)){
        printf("Account 2 written to the shared memory region\n");
    }
    if(tm_write(shared, tx, account3, sizeof(BankAccount), sharedRegion + 2*alignment)){
        printf("Account 3 written to the shared memory region\n");
    }
    if(tm_write(shared, tx, account4, sizeof(BankAccount), sharedRegion + 3*alignment)){
        printf("Account 4 written to the shared memory region\n");
    }
    // Commit the transaction
    if(tm_end(shared, tx)){
        printf("Transaction committed\n");
    }
    else{
        printf("Transaction aborted\n");
    }
    thread1(shared);
}

void oldTest(){
    // Print size of void pointer
    printf("Size of void pointer: %zu\n", sizeof(void*));
    // Crete a shared memory region (the alignment must be a power of 2 and at least sizeof(void*))
    shared_t shared = tm_create(1024, 16);
    // Create a transaction
    tx_t tx = tm_begin(shared, false);
    // Create stuff in private memory first
    void* ptr = aligned_alloc(tm_align(shared), 16);
    ptr = 12;
    // Print private memory value
    printf("Private memory value: %d\n", ptr);
    // Write to first segment of region
    bool success = tm_write(shared, tx, &ptr, 16, tm_start(shared));
    if(success){
        printf("Write successful\n");
    } else {
        printf("Write failed\n");
    }
    // Read from first segment of region
    void* empty = aligned_alloc(tm_align(shared), 16);
    tm_read(shared, tx, tm_start(shared), 16, &empty);
    // Print read value
    printf("Read value: %d\n", empty);
}