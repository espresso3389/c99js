int main(void) {
    unsigned int size = 42;
    size = (size + 7) & ~(unsigned int)7;
    return (int)size;
}
