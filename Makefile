COMPILER = g++
UNAME := $(shell uname)

LIBS = -l boost_program_options -l pthread -l z
BOOST_INCLUDE = /usr/include
ifeq ($(UNAME), FreeBSD)
LIBS = -l boost_program_options	-l pthread -l z -l compat
BOOST_INCLUDE = /usr/local/include
endif
ifeq "CYGWIN" "$(findstring CYGWIN,$(UNAME))"
LIBS = -l boost_program_options-mt -l pthread -l z
BOOST_INCLUDE = /usr/include
endif
ifeq ($(UNAME), Darwin)
LIBS = -lboost_program_options-mt -lboost_serialization-mt -l pthread -l z
BOOST_INCLUDE = /usr/local/include
endif

BOOST_LIBRARY = /usr/local/lib

ARCH = $(shell test `g++ -v 2>&1 | tail -1 | cut -d ' ' -f 3 | cut -d '.' -f 1,2` \< 4.3 && echo -march=nocona || echo -march=native)

#LIBS = -l boost_program_options-gcc34 -l pthread -l z

OPTIM_FLAGS = -O3 -fomit-frame-pointer -fno-strict-aliasing -ffast-math #uncomment for speed, comment for testability
ifeq ($(UNAME), FreeBSD)

WARN_FLAGS = -Wall
else
WARN_FLAGS = -Wall -pedantic
endif

# for normal fast execution.
FLAGS = $(ARCH) $(WARN_FLAGS) $(OPTIM_FLAGS) -D_FILE_OFFSET_BITS=64 -I $(BOOST_INCLUDE) #-DVW_LDA_NO_SSE

# for profiling
#FLAGS = $(ARCH) $(WARN_FLAGS) -O3 -fno-strict-aliasing -ffast-math -D_FILE_OFFSET_BITS=64 -I $(BOOST_INCLUDE) -pg #-DVW_LDA_NO_SSE

# for valgrind
#FLAGS = $(ARCH) $(WARN_FLAGS) -ffast-math -D_FILE_OFFSET_BITS=64 -I $(BOOST_INCLUDE) -g -O0

# for valgrind profiling: run 'valgrind --tool=callgrind PROGRAM' then 'callgrind_annotate --tree=both --inclusive=yes'
#FLAGS = -Wall $(ARCH) -ffast-math -D_FILE_OFFSET_BITS=64 -I $(BOOST_INCLUDE) -g -O3 -fomit-frame-pointer -ffast-math -fno-strict-aliasing

BINARIES = vw active_interactor
MANPAGES = vw.1

all:	vw spanning_tree library_example

%.1:	%
	help2man --no-info --name="Vowpal Wabbit -- fast online learning tool" ./$< > $@

export

spanning_tree: 
	cd cluster; $(MAKE)

vw:
	cd vowpalwabbit; $(MAKE) things

active_interactor:
	cd vowpalwabbit; $(MAKE)

library_example: vw
	cd library; $(MAKE)

.FORCE:

test: .FORCE
	@echo "vw running test-suite..."
	(cd test && ./RunTests -d -fe -E 0.001 ../vowpalwabbit/vw ../vowpalwabbit/vw)

install: $(BINARIES)
	cd vowpalwabbit; cp $(BINARIES) /usr/local/bin; cd ../cluster; $(MAKE) install

clean:
	cd vowpalwabbit; $(MAKE) clean; cd ../cluster; $(MAKE) clean
