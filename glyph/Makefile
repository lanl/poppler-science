CC = clang++

FLAGS = -O3 -Wall
OPENMP = -Xpreprocessor -fopenmp

# Using "-L/usr/local/lib -Wl,-rpath,/usr/local/lib" allows libomp to be linked *without* the need to
# export DYLD_LIBRARY_PATH=/usr/local/lib/ (which can cause system issues due to the custom clang installing in /usr/local/lib)
# Adding the "-lunwind" avoids a warning message from the linker
LIBS = $(HOME)/BioRAD/3d_kd_nlp/glyph/ncurses/lib/libncursesw.a -L$(HOME)/gsl/lib -L/usr/local/lib -Wl,-rpath,/usr/local/lib -lomp -lunwind -lz -lgsl -lgslcblas # lomp is in /usr/local/lib

PROFILE = -g #-pg	
INC = -I. -I $(HOME)/gsl/include -I$(HOME)/BioRAD/3d_kd_nlp/glyph/ncurses/include

.SUFFIXES : .o .cpp
.cpp.o:
	$(CC) $(FLAGS) $(PROFILE) $(OPENMP) $(INC) -c $<
 
all: unique_glyph select_glyph

unique_glyph: unique_glyph.o
	$(CC) $(PROFILE) -o unique_glyph unique_glyph.o $(OPENMP) $(LIBS)

select_glyph: select_glyph.o
	$(CC) $(PROFILE) -o select_glyph select_glyph.o $(OPENMP) $(LIBS)

clean:
	-rm -f *.o


