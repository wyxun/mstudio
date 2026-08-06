# MStudio Host Tool Makefile
# Compiler: mingw-w64-x86_64-clang++

MSYS64_PATH = D:/software/msys64
CXX = $(MSYS64_PATH)/mingw64/bin/clang++
WINDRES = $(MSYS64_PATH)/mingw64/bin/windres
CFLAGS = -Wall -Wextra -O2 -g -MMD -MP
CXXFLAGS = -std=c++17 $(CFLAGS)

# Directories
SRC_DIR = src
THIRD_PARTY_DIR = thirdparty
BUILD_DIR = build

# Include Paths
INCLUDES = -I$(SRC_DIR) \
           -I$(SRC_DIR)/utils \
           -I$(THIRD_PARTY_DIR)/imgui \
           -I$(THIRD_PARTY_DIR)/imgui/backends \
           -I$(THIRD_PARTY_DIR)/implot \
           -I$(MSYS64_PATH)/mingw64/include/SDL2

# Source files (Core)
SRCS = $(wildcard $(SRC_DIR)/*.cpp) \
       $(wildcard $(SRC_DIR)/panels/*.cpp) \
       $(wildcard $(SRC_DIR)/utils/*.cpp) \
       $(SRC_DIR)/utils/mringbuf.c

# ImGui & ImPlot Sources (Only compile if they exist)
IMGUI_SRCS = $(wildcard $(THIRD_PARTY_DIR)/imgui/*.cpp) \
             $(THIRD_PARTY_DIR)/imgui/backends/imgui_impl_sdl2.cpp \
             $(THIRD_PARTY_DIR)/imgui/backends/imgui_impl_opengl3.cpp
IMPLOT_SRCS = $(wildcard $(THIRD_PARTY_DIR)/implot/*.cpp)

ALL_SRCS = $(SRCS) $(IMGUI_SRCS) $(IMPLOT_SRCS)

# Object files (Flattened in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(notdir $(ALL_SRCS))))
USE_ICON ?= 1
ifeq ($(USE_ICON), 1)
    RES_OBJ = $(BUILD_DIR)/mstudio_rc.o
else
    RES_OBJ =
endif

-include $(wildcard $(BUILD_DIR)/*.d)

# Libraries
LIBS = -static -lmingw32 -lSDL2main -lSDL2 -limm32 -lole32 -loleaut32 -luuid -lversion -lwinmm -lsetupapi -lshell32 -ldinput8 -lgdi32 -ladvapi32 -lws2_32 -lopengl32 -lcomdlg32 -mwindows


TARGET = mstudio.exe

all: check_deps $(TARGET)

test: $(BUILD_DIR)/protocol_parser_test.exe
	$(BUILD_DIR)/protocol_parser_test.exe

$(BUILD_DIR)/protocol_parser_test.exe: tests/protocol_parser_test.cpp src/protocol_parser.cpp
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

check_deps:
	@if not exist $(THIRD_PARTY_DIR)/imgui/imgui.cpp exit /b 1
	@if not exist $(THIRD_PARTY_DIR)/implot/implot.cpp exit /b 1

$(TARGET): $(OBJS) $(RES_OBJ)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) $(RES_OBJ) -o $@ $(LIBS)

# Build rule for Windows resource file
$(RES_OBJ): mstudio.rc mstudio.ico
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(WINDRES) -i $< -o $@

# VPATH tells make where to look for source files
vpath %.cpp $(SRC_DIR) $(SRC_DIR)/panels $(SRC_DIR)/utils $(THIRD_PARTY_DIR)/imgui $(THIRD_PARTY_DIR)/imgui/backends $(THIRD_PARTY_DIR)/implot
vpath %.c $(SRC_DIR)/utils

# Build rule for C++ files (depends on header files to ensure clean rebuild on struct changes)
$(BUILD_DIR)/%.cpp.o: %.cpp
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build rule for C files
$(BUILD_DIR)/%.c.o: %.c
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(MSYS64_PATH)/mingw64/bin/clang $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(TARGET)

.PHONY: all clean check_deps
