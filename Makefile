BINS    = process-vma-show process-vma-mmap process-vma-fault
CFLAGS  = -Wall -g
LDFLAGS = -lpfm

all: $(BINS)

$(BINS): %: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf $(BINS)
