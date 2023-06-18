LDLIBS = -lmp3lame
BINS = enco fatcopy
all: $(BINS)
clean:
	rm -f $(BINS)

