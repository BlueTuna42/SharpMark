CXX = g++
CC = gcc
CXXFLAGS = -Wall -std=c++17 -O3
CFLAGS = -Wall -O3
LDFLAGS = -lsfml-graphics -lsfml-window -lsfml-system -lfftw3f -lraw -lm -ldl

# Папки
OBJ_DIR = obj
BIN_NAME = focus_analyzer_gui

# Исходники
SRCS_CPP = main.cpp FFT.cpp bmp.cpp scan.cpp XMP_tools.cpp
SRCS_C = tinyfiledialogs.c

# Объектные файлы теперь живут в папке obj/
OBJS = $(addprefix $(OBJ_DIR)/, $(SRCS_CPP:.cpp=.o) $(SRCS_C:.c=.o))

all: $(OBJ_DIR) $(BIN_NAME)

# Создание папки для объектных файлов
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Сборка финала
$(BIN_NAME): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Компиляция C++
$(OBJ_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Компиляция C (tinyfiledialogs)
$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Очистка всего лишнего
clean:
	rm -rf $(OBJ_DIR)
	rm -f $(BIN_NAME) focus_checker BlurryList.txt
	@echo "Cleaned up!"

.PHONY: all clean