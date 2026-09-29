CXX ?= g++
CXXFLAGS ?= -O2 -pthread -std=gnu++17 -Wall -Wextra -Wno-class-memaccess
INCLUDES = -I.. -I../esp32/include -I.

emergent-sim: main.cpp world.h sim_body.h body_plan.h ../organism.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp -o $@

test: ../organism_test.cpp ../organism.h
	$(CXX) $(CXXFLAGS) -I.. ../organism_test.cpp -o organism-test && ./organism-test

clean:
	rm -f emergent-sim organism-test

.PHONY: clean test
