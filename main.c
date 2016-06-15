#include <stdio.h>
#include "btree.h"
#include <stdlib.h>
#include <time.h>
int uniqueRandom(int size);

int main(int argc, const char * argv[]) {
    bt_payload entry, *found;
    BTHANDLE tree = bt_create("teste", 64);
    srand(time(NULL));
	uniqueRandom(100);
    /* insert 100 random entries */
    for (int i = 0; i < 100; i++) {
        entry.key = uniqueRandom(0);
        entry.value = i;
        bt_put(tree, entry);
    }
    /* search 100 random entries */
    for (int i = 0; i < 100; i++) {
        found = bt_get(tree, i);
        if (found) {
            printf("Chave: %u\n Valor: %u\n\n", found->key, found->value);
        }
    }
    return 0;
}

/* If |size| is > 0 the function will generate a static array with
 |size| elements. On subsequent calls size must be 0 and the function
 will return an unique random number in the range 0 to |size| */

int uniqueRandom(int size) {
    int i, n;
    static int numNums = 0;
    static int *numArr = NULL;
    if (size > 0) {
        numArr = malloc (sizeof(int) * size);
        for (i = 0; i  < size; i++) numArr[i] = i;
        numNums = size;
        return 0;
    }
    if (numNums == 0)  /* no more numbers left */
        return 0;
    
    n = rand() % numNums;
    i = numArr[n];
    numArr[n] = numArr[numNums-1];
    numNums--;
    if (numNums == 0) {  /*free numArr when there is no more unique numbers */
        free (numArr);
        numArr = 0;
    }
    return i;
}
