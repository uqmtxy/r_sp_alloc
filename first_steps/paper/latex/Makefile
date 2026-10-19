PDFLATEX ?= pdflatex
BIBTEX ?= bibtex

all: main.pdf

main.pdf: main.tex references.bib
	$(PDFLATEX) main.tex
	$(BIBTEX) main
	$(PDFLATEX) main.tex
	$(PDFLATEX) main.tex

clean:
	rm -f *.aux *.bbl *.blg *.log *.out *.pdf
