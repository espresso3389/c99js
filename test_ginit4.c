int is_known(const char *name) {
    static const char *names[] = {"foo", "bar", 0};
    int i;
    for (i = 0; names[i]; i++) {
    }
    return i;
}
int main(void) { return is_known("x"); }
