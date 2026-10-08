CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pthread
CPPFLAGS := -I$(CURDIR)/src
BUILD_DIR := $(CURDIR)/build

SHARED_SRCS := \
    $(CURDIR)/src/common/logger.cpp \
    $(CURDIR)/src/domain/job.cpp \
    $(CURDIR)/src/domain/job_manager.cpp \
    $(CURDIR)/src/domain/in_memory_job_store.cpp \
    $(CURDIR)/src/io/reactor.cpp \
    $(CURDIR)/src/network/connection.cpp \
    $(CURDIR)/src/network/unix_socket_listener.cpp \
    $(CURDIR)/src/process/posix_process_launcher.cpp \
    $(CURDIR)/src/protocol/frame_codec.cpp \
    $(CURDIR)/src/server/request_handler.cpp

CLIENT_SRCS := $(CURDIR)/src/client/main.cpp $(SHARED_SRCS)
SERVER_SRCS := $(CURDIR)/src/server/main.cpp $(SHARED_SRCS)

CLIENT_BIN := $(BUILD_DIR)/jobrunner-cli
SERVER_BIN := $(BUILD_DIR)/jobrunner-server

.DEFAULT_GOAL := JuanPotrillo

.PHONY: all JuanPotrillo clean

all: JuanPotrillo

$(BUILD_DIR):
    mkdir -p $(BUILD_DIR)

JuanPotrillo: $(CLIENT_BIN) $(SERVER_BIN)
    @echo "Compilacion completada: $(CLIENT_BIN) y $(SERVER_BIN)"

$(CLIENT_BIN): $(CLIENT_SRCS) | $(BUILD_DIR)
    $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(CLIENT_SRCS) -o $@

$(SERVER_BIN): $(SERVER_SRCS) | $(BUILD_DIR)
    $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SERVER_SRCS) -o $@

clean:
    rm -rf $(BUILD_DIR)