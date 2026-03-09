CXX = g++
CXXFLAGS = -std=c++14 -O3 -Wall -Wextra

SRCS = test_main.cpp MiniGoEngine_Debug.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = test_ai

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
