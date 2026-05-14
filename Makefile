CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -O3
LDFLAGS_GUI = -lsfml-graphics -lsfml-window -lsfml-system

SRC_BENCH = src/main.cpp
SRC_GUI = src/gui_main.cpp
SRC_TEST = src/test_env.cpp

TARGET_BENCH = bin/benchmark
TARGET_GUI = bin/sfml-gui
TARGET_TEST = bin/test-env

all: $(TARGET_BENCH) $(TARGET_GUI) $(TARGET_TEST)

$(TARGET_BENCH): $(SRC_BENCH)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC_BENCH) -o $(TARGET_BENCH)

$(TARGET_GUI): $(SRC_GUI)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC_GUI) -o $(TARGET_GUI) $(LDFLAGS_GUI)

$(TARGET_TEST): $(SRC_TEST)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC_TEST) -o $(TARGET_TEST)

clean:
	rm -rf bin
