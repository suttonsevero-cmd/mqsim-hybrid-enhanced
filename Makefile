CC        := g++
LD        := g++
CC_FLAGS  := -std=c++11 -O3 -g

MODULES   := exec host nvm_chip nvm_chip/flash_memory sim ssd utils
SRC_DIR   := $(addprefix src/,$(MODULES)) src
BUILD_DIR := $(addprefix build/,$(MODULES)) build

SRC       := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.cpp))
SRC       := src/main.cpp $(SRC)
OBJ       := $(patsubst src/%.cpp,build/%.o,$(SRC))
INCLUDES  := $(addprefix -I,$(SRC_DIR))

vpath %.cpp $(SRC_DIR)

ifeq ($(OS),Windows_NT)
    MAIN_NAME := MQSim.exe

    RM_DIR    := rmdir /s /q
    RM_FILE   := del /f /q

    fix_path  = $(subst /,\,$1)

    MKDIR     = if not exist $(call fix_path,$1) mkdir $(call fix_path,$1)
else
    MAIN_NAME := MQSim

    RM_DIR    := rm -rf
    RM_FILE   := rm -f

    fix_path  = $1

    MKDIR     = mkdir -p $1
endif
# ---------------------------------------------------------

define make-goal
$1/%.o: %.cpp
	$(CC) $(CC_FLAGS) $(INCLUDES) -c $$< -o $$@
endef

.PHONY: all checkdirs clean

all: checkdirs $(MAIN_NAME)

$(MAIN_NAME): $(OBJ)
	$(LD) $^ -o $@

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	$(call MKDIR,$@)

clean:
	$(RM_DIR) $(call fix_path, build)
	$(RM_FILE) $(MAIN_NAME)

$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))