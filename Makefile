CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

LIB_SRCS = parser.cpp policy.cpp scheduler.cpp workload.cpp eval.cpp report.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)
HEADERS = $(wildcard *.h)

all: kubesched

kubesched: main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

kubesched_tests: tests.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: kubesched_tests
	./kubesched_tests

eval: kubesched
	./kubesched eval --csv results.csv

clean:
	rm -f *.o kubesched kubesched_tests

.PHONY: all test eval clean
