struct Block {
    int next;
    int size;
    int used;
    char data[];
};

int test(struct Block *blk) {
    char *ptr = blk->data + blk->used;
    return (int)ptr;
}

int main(void) {
    return 0;
}
