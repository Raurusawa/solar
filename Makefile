MINGW = C:/msys64/mingw64
CXX = $(MINGW)/bin/g++
CXXFLAGS = -std=c++11 -Wall
LDFLAGS = -lglfw3 -lglew32 -lopengl32
SRC = src/main.cpp src/config.cpp src/camera.cpp src/shader.cpp src/sphere.cpp src/planet.cpp src/texture.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = solar_system.exe

# 确保 MSYS2 DLL 在 PATH 中
export PATH := $(MINGW)/bin:$(PATH)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJ) $(TARGET)

.PHONY: all clean
