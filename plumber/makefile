# Default compile parameters
color?=yes

BIN_DIR=bin
CC=g++-8

CPP_FILES=$(shell find . -type f -name '*.cpp')
HEADER_FILES=$(shell find . -type f \( -iname \*.hpp -o -iname \*.h \))

INCLUDE_FOLDERS=$(shell find . -type d -name 'include')
SRC_FOLDERS=$(shell find . -type d -name 'src')

CPP_INCLUDE=$(foreach dir,$(INCLUDE_FOLDERS),-I$(dir))
CPP_SOURCE=$(foreach dir,$(SRC_FOLDERS),$(dir)/*.cpp)


CPP_FLAGS=-std=c++11 -O3 -pthread -lpthread -Wall -Wextra -Werror -Wno-format -pedantic-errors $(CPP_INCLUDE)
ifeq (color, yes)
all::
	CPP_FLAGS += -fdiagnostics-color=always
endif

COMPILE=$(CC) $(CPP_FLAGS) $(CPP_SOURCE)

RELEASE_FLAGS=-funroll-loops


NAME=plumber

BIN_FILE=$(BIN_DIR)/$(NAME)
EXEC=$(BIN_FILE)


all: buildpath $(EXEC)

$(EXEC): $(CPP_FILES) $(HEADER_FILES)
	$(COMPILE) $(RELEASE_FLAGS) -o $(EXEC)
	
buildpath:
	@if [ ! -d "$(BIN_DIR)" ]; then mkdir $(BIN_DIR); fi

clean:
	if [ -d "$(BIN_DIR)" ]; then rm -R $(BIN_DIR); fi

