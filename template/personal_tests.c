//
// Created by Aybars Yazici on 5.11.2022.
//

#include <stdio.h>
#include "tm.h"

// Function to check if given pointer is aligned to given byte size
bool is_aligned(void *ptr, size_t align) {
    return (uintptr_t) ptr % align == 0;
}

int main(){
    // Print size of void pointer
    printf("Size of void pointer: %zu\n", sizeof(void*));
    // Crete a shared memory region (the alignment must be a power of 2 and at least sizeof(void*))
    shared_t shared = tm_create(1024, 16);
    // Print the address of the region
    printf("Address of the region: %p\n", shared);
    // Print the size of the region
    printf("Size of the region: %zu\n", tm_size(shared));
    // Print the alignment of the region
    printf("Alignment of the region: %zu\n", tm_align(shared));
    // Try simple pointer arithmetic
    printf("Address of the region + 1: %p\n", (void*)shared + 1);
    // Print the first memory segment of the region
    printf("First memory segment of the region: %p\n", tm_start(shared));
    // Pointer arithmetic with the first memory segment of the region
    printf("First memory segment of the region + 1: %p\n", tm_start(shared) + 1);
    // Check if the first memory segment of the region is aligned
    printf("Is the first memory segment of the region aligned: %d\n", is_aligned(tm_start(shared), 3));
    printf("___________________\n");

    // Create an integer array and a pointer to it
    int arr[10];
    int *ptr = arr;
    // Print the address of the array
    printf("Address of the array: %p\n", arr);
    // Print the address of the pointer
    printf("Address of the pointer: %p\n", ptr);
    // Print the address of the pointer + 1
    printf("Address of the pointer + 1: %p\n", ptr + 1);
    // Print address off 2nd element of array
    printf("2nd element of array: %p\n", &arr[1]);


}