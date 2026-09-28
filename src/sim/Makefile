CXX ?= g++
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wextra -Wno-class-memaccess
INCLUDES = -I../esp32/include -I.

emergent-sim: main.cpp world.h sim_body.h body_plan.h $(wildcard ../esp32/include/core/*.h)
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp -o $@

clean:
	rm -f emergent-sim

.PHONY: clean
