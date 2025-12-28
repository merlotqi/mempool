CXX			:= g++
CXXFLAGS 	:= -std=c++17 -O3 -Wall -Wextra -Wpedantic -march=native -I.
LDFLAGS 	:= -lpthread -lrt

TARGET		:= test_suite
SRC			:= tests/test_main.cpp
DEP			:= mempool.hpp

GREEN		:= \033[1;32m
RESET		:= \033[0m

all: $(TARGET)
		@echo "$(GREEN)>>> Running tests...$(RESET)"
		@./$(TARGET)

$(TARGET): $(SRC) $(DEP)
	@echo "$(GREEN)>>> Compiling $@...$(RESET)"
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	@echo "Cleaning up..."
	rm -f $(TARGET)

.PHONY: all clean