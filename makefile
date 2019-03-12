# Default compile parameters
color?=yes

OBJ_DIR=obj
BIN_DIR=bin
CC=g++-8

CPP_FILES=$(shell find . -type f -name '*.cpp')
HEADER_FILES=$(shell find . -type f \( -iname \*.hpp -o -iname \*.h \))

INCLUDE_FOLDERS=$(shell find . -type d -name 'include')
SRC_FOLDERS=$(shell find . -type d -name 'src')

CPP_INCLUDE=$(foreach dir,$(INCLUDE_FOLDERS),-I$(dir))
CPP_SOURCE=$(foreach dir,$(SRC_FOLDERS),$(dir)/*.cpp)


CPP_FLAGS=-std=c++11 -O3 -Wall -Wextra -Werror -Wno-format -pedantic-errors $(CPP_INCLUDE)
ifeq (color, yes)
all::
	CPP_FLAGS += -fdiagnostics-color=always
endif

SHARED_LIB_FLAGS=-c -fPIC
CPP_DEFINE=
COMPILE=$(CC) $(CPP_FLAGS) $(CPP_DEFINE) $(CPP_SOURCE)

RELEASE_FLAGS=-funroll-loops


NAME=plumber

BIN_FILE=$(BIN_DIR)/$(NAME)
EXEC=$(BIN_FILE)


all: buildpath $(EXEC)

$(EXEC): $(CPP_FILES) $(HEADER_FILES)
	$(COMPILE) $(RELEASE_FLAGS) $(SHARED_LIB_FLAGS)
	@rm -f $(OBJ_DIR)/*.o
	@mv *.o $(OBJ_DIR)/
	g++ -shared -Wl,-soname,$(EXEC) -o $(EXEC)  $(OBJ_DIR)/*.o
	
buildpath:
	@if [ ! -d "$(OBJ_DIR)" ]; then mkdir $(OBJ_DIR); fi
	@if [ ! -d "$(BIN_DIR)" ]; then mkdir $(BIN_DIR); fi

clean:
	rm -f *.o
	if [ -d "$(OBJ_DIR)" ]; then rm -R $(OBJ_DIR); fi
	if [ -d "$(PERF_DIR)" ]; then rm -R $(PERF_DIR); fi

clean-all: clean
	if [ -d "$(BIN_DIR)" ]; then rm -R $(BIN_DIR); fi

