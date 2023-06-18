LDLIBS = -lmp3lame
BINS = enco
all: $(BINS)
clean:
	rm -f $(BINS)

