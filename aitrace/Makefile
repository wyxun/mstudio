# AITrace CLI Tool Makefile
# Links against libmstudiocore.a (built by ../core/Makefile)

MSYS64_PATH = D:/software/msys64
CXX = $(MSYS64_PATH)/mingw64/bin/clang++
CFLAGS = -Wall -Wextra -O2 -g
CXXFLAGS = -std=c++17 $(CFLAGS)

SRC_DIR = src
CORE_DIR = ../core
CORE_BUILD = $(CORE_DIR)/build
SRC_TOP = ../src
BUILD_DIR = build

INCLUDES = -I$(SRC_DIR) -I$(SRC_TOP) -I$(SRC_TOP)/utils

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(notdir $(SRCS))))

TARGET = aitrace.exe
LIB_CORE = $(CORE_BUILD)/libmstudiocore.a
LIBS = -lws2_32

all: core $(TARGET)

core:
	@$(MAKE) -C $(CORE_DIR)

$(TARGET): $(OBJS) $(LIB_CORE)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LIB_CORE) -o $@ $(LIBS)

vpath %.cpp $(SRC_DIR)

$(BUILD_DIR)/%.cpp.o: %.cpp
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(TARGET)

clean-all: clean
	@$(MAKE) -C $(CORE_DIR) clean

.PHONY: all clean clean-all core
