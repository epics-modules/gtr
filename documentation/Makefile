BOOK_HTMLFLAGS = --book --duplex --no-numbered --format pdf14 --title --size letter
NOTE_HTMLFLAGS = --continuous --no-numbered --format pdf14 --size letter

all: gtr.pdf knownProblems.pdf releaseNotes.pdf

%.pdf: %.html
	-htmldoc $(NOTE_HTMLFLAGS) --outfile $@ $<

gtr.pdf: gtr.html
	-htmldoc $(BOOK_HTMLFLAGS) --outfile $@ $<

clean:

realclean: clean
	rm -f *.pdf
