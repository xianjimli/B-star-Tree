 /* June of 2016
 ** Cristiano Carvalho Lacerda <cristiano.lacerda@usp.br>
 ** This work is free. You can redistribute, modify it, or do
 ** whatever the fuck you want.
 **
 ** This code provide a simple working example of a disk based b* tree.
 ** This is not a eficient, robust or even safe libary but instead aims
 ** to be very easy to understand.
 ** Github offer plenty of great options of open source b and b+ C
 ** libraries but does not seem to have any b * code.
 ** For state of art implementations of b trees one can refer to the
 ** SQLite and MacFS source code. These provides an unvaluable insight
 ** of how and why b trees (and it's variants) are used to power most
 ** of the majors DBMS and file systems for almost half a ceuntry.
 **
 ** This implementation is based on the pseudocode presented at:
 **      Introduction to algorithms / CORMER [et al.]
 **      Pages 486 - 504, MIT PRESS 3th ed.
 ** Which provides a detailed introduction to b trees, howover does
 ** not present the b star tree.
 ** For a discussion on b tress variants please refer to:
 **     THE ART OF COMPUTER PROGRAMMING, Volume 3 / KNUTH
 **     Pages 472-480. Addison-Wesley Publishing Company.
 **
 ** The main diference is the introduction of the overflow operation
 ** which allow a node to borrow/lend keys from it's siblings.
 ** The split operation is then postponed for only when a node and one
 ** of its brothers are full so it's possible to split 2 nodes into 3.
 ** This allow for a more campact and faster and tree.
 **
 ** The following content from btreeInt.h in SQLite source provides the
 ** very fundamental introduction to b trees.
 **
 ** The basic idea is that each node of the tree contains N - 1 entires
 ** and N pointers to subtrees. This is the order of the tree.
 **
 **   ----------------------------------------------------------------
 **   | Ptr(0) | Key(0) | Ptr(1) | Key(1) | ... | Key(N - 1) | Ptr(N)|
 **   ----------------------------------------------------------------
 **
 ** All of the keys on the node that Ptr(0) points to have values less
 ** than Key(0). All of the keys on the node Ptr(1) and its subpages have
 ** values greater than Key(0) and less than Key(1). All of the keys
 ** on Ptr(N) and its subpages have values greater than Key(N -1).
 **
 ** Implementation details:
 ** The key and data for any entry are combined to form the "payload",
 ** The payload and the preceding pointer are combined to form a "Cell".
 ** Each node has a small header which contains the Ptr(N) and other info
 ** such as the current number of items stored.
 ** A node is just a header, an array of |order - 1| cells and maybe some
 ** unused space at the end so each node has the same size of a page disk.
 ** 
 ** Incomplete features:
 ** Delete, redistribuition.
 **
 ** Know Bugs:
 ** Seach doesn't work proprely (LOL)
 ** Also fix all the memleaks.
 */

#include "btree.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* Private structures */
typedef struct _bt_header {
    u8 isLeaf;
    u8 height;
    u16 activeCount;
    u32 lastChildren; /* (this is Ptr(N)) */
} __attribute__((packed)) bt_header;

typedef struct _bt_cell {
    u32 leftChildOffset;
    bt_payload payload;
} __attribute__((packed)) bt_cell;

typedef struct  _bt_node {
    bt_header info;
    bt_cell cells[];
} __attribute__((packed)) bt_node;

typedef struct _bt_page {
    bt_node *node;
    u32 diskOffset;
} __attribute__((packed)) bt_page;

typedef struct _bt_tree {
    FILE *btFile;
    char indexName[32];
    int order;
    unsigned int pageSize;
    bt_page *root;
} bt_tree;

/* Private functions */
bt_node *bt_newNode(unsigned int PageSize);
bt_page *bt_newPage(bt_tree *bt);
bt_page *bt_readPage(bt_tree *bt, u32 nodeOffset);
int bt_saveNode(bt_tree *bt, bt_page *p);
void bt_splidChild(bt_tree *bt, bt_page *px, bt_page *py,u16 i);
void bt_splidChild3way(bt_tree *bt, bt_page *px, bt_page *py, bt_page *pz, u16 i);
void bt_insertNonFull(bt_tree *bt, bt_page *node, bt_payload entry);
void bt_rotateRight(bt_tree *bt, bt_page *pp, bt_page *pparent, bt_page *pq, u16 k);
void bt_rotateLeft(bt_tree *bt, bt_page *pp, bt_page *pparent, bt_page *pq, u16 k);

/* ** */
BTHANDLE bt_create(const char *name, unsigned int pageSize) {
    bt_tree *bt = malloc(sizeof(*bt));
    strcpy(bt->indexName, name);
    strcat(bt->indexName, ".idx");
    bt->pageSize = pageSize;
    bt->order = ((pageSize - sizeof(bt_header)) / sizeof(bt_cell)) + 1;
    /* create file */
    bt->btFile = fopen(bt->indexName, "w");
    fclose(bt->btFile);
    /* save root */
    bt->root = bt_newPage(bt);
    return bt;
}

bt_node *bt_newNode(unsigned int pageSize) {
    bt_node *n = malloc(pageSize);
    /* initialize node */
    n->info.activeCount = 0;
    n->info.isLeaf = 1;
    return n;
}

bt_page *bt_newPage(bt_tree *bt) {
    bt_page *p = malloc(sizeof(*p));
    p->node = bt_newNode(bt->pageSize);
    bt->btFile= fopen(bt->indexName, "r+");
    setbuf(bt->btFile, NULL); /* no buffer */
    fseek(bt->btFile, 0, SEEK_END);
    p->diskOffset = (u32) ftell(bt->btFile);
    bt_saveNode(bt, p);
    return p;
}

bt_page *bt_readPage(bt_tree *bt, u32 nodeOffset) {
    /* allocate space in memory fo node */
    bt_page *p = malloc(sizeof(*p));
    p->node = bt_newNode(bt->pageSize);
    /* read content pointed by nodeOffset from file */
    bt->btFile = fopen(bt->indexName, "r+");
    setbuf(bt->btFile, NULL); /* no buffer */
    fseek(bt->btFile, nodeOffset, SEEK_SET);
    fread(p->node, bt->pageSize, 1, bt->btFile);
    fclose(bt->btFile);
    p->diskOffset = nodeOffset;
    return p;
}

int bt_saveNode(bt_tree *bt, bt_page *p) {
    bt->btFile = fopen(bt->indexName, "r+");
    setbuf(bt->btFile, NULL); /* no buffer */
    fseek(bt->btFile, p->diskOffset, SEEK_SET);
    fwrite(p->node, bt->pageSize,1, bt->btFile);
    fclose(bt->btFile);
    return 0;
}

bt_payload* bt_get2(bt_tree *bt, bt_node *x, int key) {
    int i = x->info.activeCount;
    bt_page *child;
    while (i >= 0 && key < x->cells[i].payload.key) {
        i--;
    }
    if (key == x->cells[i].payload.key) {
        bt_payload *bt = malloc(sizeof(*bt));
        *bt = x->cells[i].payload;
        return bt;
    }
    if (x->info.isLeaf) {
        return NULL;
    }
    else {
        if (i == x->info.activeCount - 1) {
        /* entry key is bigger than every key in the node */
        child = bt_readPage(bt, x->info.lastChildren);
        }
        else {
        child = bt_readPage(bt, x->cells[++i].leftChildOffset);
        }
    }
    return bt_get2(bt, child->node, key);
}

bt_payload* bt_get(BTHANDLE h, int key) {
    bt_tree *bt = (bt_tree*) h;
    bt_node *x = bt->root->node;
    return bt_get2(bt, x, key);
}

void bt_splidChild(bt_tree *bt, bt_page *childp, bt_page *parentp, u16 i) {
    bt_node *parent = parentp->node, *child = childp->node;
    int t =  (bt->order - 1) / 2; /* t is the minimum number of keys */
    int splitPoint = (bt->order - 1 ) - t;
    bt_page *pz = bt_newPage(bt); /* this will be right sibling of y */
    bt_node *z = pz->node; /* shorthand */
    /* setting z values */
    z->info = child->info;
    z->info.activeCount = splitPoint - 1;
    /* copy the higher order keys to z */
    for (int j = 0; j < z->info.activeCount; ++j) {
        z->cells[j] = child->cells[j + t + 1];
    }
    /* fix values of y */
    child->info.activeCount = t;
    child->info.lastChildren = child->cells[t].leftChildOffset;
    /* make room for new key in x */
    for (int j = parent->info.activeCount; j > i; --j) {
        parent->cells[j + i] = parent->cells[j];
    }
    /* copy promoted key to x */
    parent->cells[i].payload = child->cells[t].payload;
    parent->info.activeCount++;
    /* make z the right sibling of y */
    parent->info.lastChildren = pz->diskOffset;
    /* save changes to disk */
    bt_saveNode(bt, parentp);
    bt_saveNode(bt, childp);
    bt_saveNode(bt, pz);
}

void bt_splidChild3way(bt_tree *bt, bt_page *parentp, bt_page *leftp, bt_page *rightp, u16 i) {
    bt_node *parent = parentp->node, *left = leftp->node, *right = rightp->node;
    bt_page *midp = bt_newPage(bt);
    bt_node *mid = midp->node;
    
    int t = (2 * bt->order - 1) / 3;
    int split = (bt->order - 1 ) - t;
    /* move first higher key in left to parent i position*/
    bt_payload parentPayload = parent->cells[i].payload; /*save it for latter */
    parent->cells[i].payload = left->cells[t].payload;
    /* copy higher order keys from left to mid */
    for (int j = 0; j < split - 1; ++j) {
        mid->cells[j] = left->cells[j + t + 1];
    }
    /* put parent i key in split position of mid */
    mid->cells[split - 1].payload = parentPayload;
    mid->cells[split - 1].leftChildOffset = left->info.lastChildren;
    /* fix node headers */
    mid->info = left->info;
    mid->info.activeCount = t;
    left->info.activeCount = t;
    left->info.lastChildren = left->cells[t].leftChildOffset;
    right->info.activeCount = (split - 1) + (split - 1)  + 1;
    /* make room for new key in parent */
    for (int j = parent->info.activeCount; j > i; --j) {
        parent->cells[j + i] = parent->cells[j];
    }
    /* promote lowest key from right to parent */
    parent->cells[i + 1].payload = right->cells[t - 1].payload;
    parent->cells[i + 1].leftChildOffset = midp->diskOffset;
    mid->info.lastChildren = right->cells[t - 1].leftChildOffset;
    /* copy lower order keys from rgiht to mid */
    for (int j = 0; j < split; ++j) {
        mid->cells[split + j] = right->cells[j];
    }
    /* shift the cells in right to begning of array */
    for (int j = 0; j < right->info.activeCount; ++j) {
        right->cells[j] = right->cells[j + t];
    }
    /* save changes to disk */
    bt_saveNode(bt, parentp);
    bt_saveNode(bt, leftp);
    bt_saveNode(bt, midp);
    bt_saveNode(bt, rightp);
}
/* The rotation methods are very expansive since it requires one disk access
 * and just move one key, a better aproach to implement the overflow
 * operation is to redistribuite the keys evenly beetween the nodes */
void bt_rotateRight(bt_tree *bt, bt_page *pp, bt_page *pparent, bt_page *pq, u16 k) {
    /* useful shorthand for refering to the nodes */
    bt_node *p = pp->node, *parent = pparent->node, *q = pq->node;
    /* shift all records in q one position right */
    for (int i = q->info.activeCount; i > 0; --i) {
        q->cells[i + 1] = q->cells[i];
    }
    /* move the k entry in parent to first position of q */
    q->cells[0].payload = p->cells[k].payload;
    /* promote largest key from p to parent k position */
    parent->cells[k].payload = p->cells[p->info.activeCount].payload;
    /* adjust pointers */
    q->cells[0].leftChildOffset = p->info.lastChildren;
    p->info.lastChildren = p->cells[--p->info.activeCount].leftChildOffset;
    /* set new size of q */
    q->info.activeCount++;
    /* write changes to disk */
    bt_saveNode(bt, pp);
    bt_saveNode(bt, pparent);
    bt_saveNode(bt, pq);
}

void bt_rotateLeft(bt_tree *bt, bt_page *pp, bt_page *pparent, bt_page *pq, u16 k) {
    /* useful shorthand for refering to the nodes */
    bt_node *p = pp->node, *parent = pparent->node, *q = pq->node;
    /* copy the k entry in parent to last position of p */
    p->cells[p->info.activeCount].payload = parent->cells[k].payload;
    /* copy smallest key from q to parent k position */
    parent->cells[k].payload = q->cells[0].payload;
    /* fix pointers */
    p->cells[++p->info.activeCount].leftChildOffset = p->info.lastChildren;
    p->info.lastChildren = q->cells[0].leftChildOffset;
    /* shift all records in q one position left */
    for (int i = 0; i < q->info.activeCount; ++i) {
        q->cells[i] = q->cells[i + 1];
    }
    /* set new size of q */
    q->info.activeCount--;
    /* write changes to disk */
    bt_saveNode(bt, pp);
    bt_saveNode(bt, pparent);
    bt_saveNode(bt, pq);
}

int bt_put(BTHANDLE h, bt_payload entry) {
    bt_tree *bt = (bt_tree*) h;
    bt_page *root = bt->root;
    /* if root is full */
    if ( root->node->info.activeCount == (((bt_tree *)bt)->order - 1) ) {
        /* create and set new root */
        bt_page *newRoot = bt_newPage(bt);
        newRoot->node->cells[0].leftChildOffset = root->diskOffset;
        newRoot->node->info.isLeaf = 0;
        newRoot->node->info.activeCount = 0;
        /* fix tree pointer */
        bt->root = newRoot;
        root->node->info.height++;
        root->node->info.isLeaf = 1;
        /* split old root and add entry */
        bt_splidChild(bt,root, newRoot, 0);
        bt_insertNonFull(bt, newRoot, entry);
    }
    else {
        bt_insertNonFull(bt, root, entry);
    }
    return 0;
}
/* This function is big and messy: write some auxiliary methods */
void bt_insertNonFull(bt_tree *bt, bt_page *px, bt_payload entry) {
    bt_node *x = px->node;
    int i = x->info.activeCount - 1;
    /* if x is a leaf insert the entry in the correct position */
    if (x->info.isLeaf) {
        while (i >= 0 && entry.key < x->cells[i].payload.key) {
            x->cells[i + 1].payload = x->cells[i].payload;
            i--;
        }
        x->cells[++i].payload = entry;
        x->info.activeCount++;
        bt_saveNode(bt, px);
        return;
    }
    else {
        /* if not a leaf find the correct subtree to insert entry */
        int flag = 0; /* -1 node is the leftmost, +1 node is the rightmost */
        u32 childOffset;
        bt_page *child;
        while (i >= 0 && entry.key < x->cells[i].payload.key) {
            i--;
        }
        if (i == x->info.activeCount - 1) {
            /* entry key is bigger than every key in the node */
            childOffset = x->info.lastChildren;
            child = bt_readPage(bt, childOffset);
            flag = 1;
            
        } else {
            childOffset = x->cells[++i].leftChildOffset;
            child = bt_readPage(bt, childOffset);
            if (i == 0) flag = -1; /* entry key is smaller than every key in node */
        }
        if (child->node->info.activeCount == bt->order - 1) {
            /* try to overflow and if necessary split */
            bt_page *sibling = NULL; /* just to shut up compiler warnings */
            if (flag == -1) {
                sibling = bt_readPage(bt, x->info.lastChildren);
                if (sibling->node->info.activeCount < bt->order - 1) {
                    bt_rotateRight(bt, child, px, sibling, i);
                    bt_insertNonFull(bt, child, entry);
                    return;
                }
                else bt_splidChild3way(bt, px, child, sibling, i);
            }
            else if (flag > -1) {
                sibling = bt_readPage(bt, x->cells[i].leftChildOffset);
                if (sibling->node->info.activeCount < bt->order - 1 ) {
                    bt_rotateLeft(bt, sibling, px, child, i);
                    bt_insertNonFull(bt, child, entry);
                    return;
                }
                else bt_splidChild3way(bt, px, sibling, child, i);
            }
            bt_insertNonFull(bt, px, entry);
        }
        /* just insert in child */
        else {
            bt_insertNonFull(bt, child, entry);
        }
    }
}
