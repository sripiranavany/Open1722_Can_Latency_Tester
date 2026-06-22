# Makefile for LaTeX project

# Compiler
LATEX = pdflatex

# Source file
SRC = report.tex

# Output file
PDF = $(SRC:.tex=.pdf)

# Default target
all: $(PDF)

$(PDF): $(SRC)
	$(LATEX) $(SRC)
	$(LATEX) $(SRC)

# Clean up generated files
clean:
	rm -f *.aux *.log *.out $(PDF)

.PHONY: all clean
