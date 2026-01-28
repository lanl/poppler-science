CC = clang++

FLAGS = -O3 -Wall

# For MacOS clang users
OPENMP = -Xpreprocessor -fopenmp

# For Linux GCC users
#OPENMP = -fopenmp

# For clang++ on MacOS
# 	Using "-L/usr/local/lib -Wl,-rpath,/usr/local/lib" allows libomp to be linked *without* the need to
# 	export DYLD_LIBRARY_PATH=/usr/local/lib/ (which can cause system issues due to the custom clang installing in /usr/local/lib)
# 	Adding the "-lunwind" avoids a warning message from the linker

# The following variables definitions assume that you have ncurses installed in $(HOME)/ncurses and the Gnu Scientific Library
# installed in $(HOME)/gsl. Please edit as needed for your system!
LIBS = $(HOME)/ncurses/lib/libncursesw.a -L$(HOME)/gsl/lib -L/usr/local/lib -Wl,-rpath,/usr/local/lib -lomp -lunwind -lz -lgsl -lgslcblas # lomp is in /usr/local/lib
INC = -I. -I $(HOME)/gsl/include -I$(HOME)/ncurses/include

.SUFFIXES : .o .cpp
.cpp.o:
	$(CC) $(FLAGS) $(OPENMP) $(INC) -c $<
 
all: unique_glyph select_glyph

unique_glyph: unique_glyph.o
	$(CC) -o unique_glyph unique_glyph.o $(OPENMP) $(LIBS)

select_glyph: select_glyph.o
	$(CC) -o select_glyph select_glyph.o $(OPENMP) $(LIBS)

clean:
	-rm -f *.o


