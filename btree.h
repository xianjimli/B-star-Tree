#ifndef __FileStructs__btree__
#define __FileStructs__btree__

#include <stdio.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Exported types */
/* User can modify this to add any desirable satelite info for a key */
/* MUST NOT HAVE ANY POINTERS and must keep key */
/* Note that the size of this struct will affect the tree order */
typedef struct _bt_payload {
    u32 key;
    u32 value;
} __attribute__((packed)) bt_payload;

/* The BTHANDLE type represents an reference to the b tree
 * without exposing its implementations details
 * It's returned from the bt_create and bt_open operations
 * and should be passed to the other calls of the api */
typedef void* BTHANDLE;


/* Public API */

BTHANDLE bt_create(const char *name, unsigned int pageSize);
BTHANDLE bt_open(const char *name);
int bt_put(BTHANDLE, bt_payload entry);
bt_payload* bt_get(BTHANDLE h, int key);
//int bt_delete(BTHANDLE, int key); 
bt_payload bt_nextRecord(BTHANDLE);



#endif /* defined(__FileStructs__btree__) */
