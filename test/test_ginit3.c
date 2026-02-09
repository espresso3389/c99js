typedef struct { const char *name; int val; } Entry;
static const Entry table[] = {
    {"hello", 10},
    {"world", 20},
    {0, 0}
};
int main(void) { return table[0].val; }
