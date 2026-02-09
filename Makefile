CC = clang
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -D_CRT_SECURE_NO_WARNINGS
SRCDIR = src
OBJDIR = obj
TARGET = c99js

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/util.c \
       $(SRCDIR)/lexer.c \
       $(SRCDIR)/preprocess.c \
       $(SRCDIR)/ast.c \
       $(SRCDIR)/type.c \
       $(SRCDIR)/symtab.c \
       $(SRCDIR)/parser.c \
       $(SRCDIR)/sema.c \
       $(SRCDIR)/codegen.c

OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

all: $(OBJDIR) $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean
