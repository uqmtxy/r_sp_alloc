CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -pthread -DNDEBUG
INCLUDES ?= -Iinclude -I../../tools/adversarial -I../factory_cv_pipeline -I../yolo26n_pipeline/onnxruntime/include

all: pare_ta_bench

pare_ta_bench: pare_ta_bench.cpp include/pare_ta_resource.hpp include/pare_ta_onnx_adapter.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o pare_ta_bench pare_ta_bench.cpp

clean:
	rm -f pare_ta_bench *.json

.PHONY: all clean
