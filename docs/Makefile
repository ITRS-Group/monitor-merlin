# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line.
SPHINXOPTS    =
SPHINXBUILD   = sphinx-build
SPHINXPROJ    = Merlin
SOURCEDIR     = .
BUILDDIR      = _build
RELEASE	      =

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile

# Grabs the readme directly from the Merlin container poller repo, so we can
# use it directly in the docs.
container-poller-readme.md:
	curl https://raw.githubusercontent.com/ITRS-Group/merlin-container-poller/master/README.md -o container-poller-readme.md

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile container-poller-readme.md
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) -D release=$(RELEASE) $(O)
