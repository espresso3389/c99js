typedef unsigned int size_t;

typedef struct Block {
    struct Block *next;
    size_t size;
    size_t used;
    char data[];
} Block;

void *test(Block *b) {
    return b->data + b->used;
}

int main(void) { return 0; }
