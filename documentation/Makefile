HTMLFLAGS = --book --duplex --no-numbered --format pdf14 --title --size letter

all: gtr.pdf knownProblems.pdf releaseNotes.pdf

%.pdf: %.html
	-htmldoc $(HTMLFLAGS) --outfile $@ $<

clean:

realclean: clean
	rm -f *.pdf
