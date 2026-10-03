CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pthread
CPPFLAGS := -I$(CURDIR)/src

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

.DEFAULT_GOAL := JuanPotrillo

.PHONY: all JuanPotrillo clean

all: JuanPotrillo

JuanPotrillo: jobrunner_client jobrunner_server
	@echo "Compilacion completada: jobrunner_client y jobrunner_server"

jobrunner_client: $(CLIENT_SRCS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(CLIENT_SRCS) -o $@

jobrunner_server: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SERVER_SRCS) -o $@

clean:
	rm -f jobrunner_client jobrunner_server
