# Компиляторы
CXX = g++
CC = gcc

# Флаги: C++17 для SFML, O3 для скорости, march=native для оптимизации под процессор
CXXFLAGS = -Wall -std=c++17 -O3 -march=native
CFLAGS = -Wall -O3
LDFLAGS = -lsfml-graphics -lsfml-window -lsfml-system -lfftw3f -lraw -lm -ldl

# Папки
OBJ_DIR = obj
BIN_NAME = focus_analyzer_gui

# Исходники
SRCS_CPP = main.cpp FFT.cpp bmp.cpp scan.cpp XMP_tools.cpp
SRCS_C = tinyfiledialogs.c

# Объектные файлы в папке obj/
OBJS = $(addprefix $(OBJ_DIR)/, $(SRCS_CPP:.cpp=.o) $(SRCS_C:.c=.o))

all: $(OBJ_DIR) $(BIN_NAME)

# Режим отладки (из ветки main)
debug: CXXFLAGS += -DDEBUG_BENCHMARK
debug: all

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Сборка финального файла
$(BIN_NAME): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Компиляция C++
$(OBJ_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Компиляция C
$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(BIN_NAME) focus_checker BlurryList.txt
	@echo "Очистка завершена!"

.PHONY: all clean debug