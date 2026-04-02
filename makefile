# C++ compiler
CXX = g++

# Compiler flags: show all warnings, use C++14 standard (needed for std::make_unique), and enable optimization
CXXFLAGS = -Wall -std=c++14 -O2

# Linker flags: link fftw3 (floating point version) and libraw libraries
LDFLAGS = -lfftw3f -lraw

# Project source files
SRCS = main.cpp FFT.cpp bmp.cpp scan.cpp XMP_tools.cpp

# Object files (replace .cpp with .o)
OBJS = $(SRCS:.cpp=.o)

# Output executable name
EXEC = focus_checker

# Default rule
all: $(EXEC)

# Rule to build the final executable
$(EXEC): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Rule to compile each .cpp file into an .o file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to clean up generated files
clean:
	rm -f $(OBJS) $(EXEC)