# Builds libmstudiocore.a from original source files in ../src/
MSYS64_PATH = D:/software/msys64
CXX = $(MSYS64_PATH)/mingw64/bin/clang++
AR = $(MSYS64_PATH)/mingw64/bin/ar
CFLAGS = -Wall -Wextra -O2 -g
CXXFLAGS = -std=c++17 $(CFLAGS)

SRC_TOP = ../src
BUILD_DIR = build

INCLUDES = -I$(SRC_TOP) -I$(SRC_TOP)/utils

# Source files (original locations, not moved)
SRCS = $(SRC_TOP)/network_mgr.cpp \
       $(SRC_TOP)/protocol_parser.cpp \
       $(SRC_TOP)/utils/ocd_client.cpp \
       $(SRC_TOP)/utils/elf_parser.cpp \
       $(SRC_TOP)/utils/map_parser.cpp

OBJS = $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(notdir $(SRCS))))

TARGET = $(BUILD_DIR)/libmstudiocore.a

vpath %.cpp $(SRC_TOP) $(SRC_TOP)/utils

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(AR) rcs $@ $^

$(BUILD_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -rf $(BUILD_DIR)

.PHONY: all clean
