CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
TARGET   = fs_simulator

# Layer 4: Simulated Disk
# Layer 3: Low-Level FS Calls
# Layer 2: User System Calls
# Layer 1: User Interface (main)
SRCS = main.cpp fs.cpp lowfs.cpp disk.cpp cache.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) FILE_SYS

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run