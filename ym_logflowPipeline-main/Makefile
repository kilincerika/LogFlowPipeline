# LogFlow - Yazılım Mimarisi Dönem Projesi (Artım 5)
# Derleyici ve Bayraklar
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2
COVFLAGS = -Wall -Wextra -std=c++17 -O0 -g --coverage -fprofile-arcs -ftest-coverage

# Hedefler
TARGET = logflow
LEGACY_TARGET = pipeline
TEST_TARGET = test_runner
COV_TARGET = test_runner_cov

SRCS = main.cpp
TEST_SRCS = tests/test_runner.cpp
HEADERS = Source.hpp Sink.hpp Stage.hpp Emitter.hpp LogRecord.hpp \
          PipelineExceptions.hpp FailedRecord.hpp ErrorPolicy.hpp \
          DeadLetterSink.hpp ValidationStage.hpp RunSummary.hpp \
          FileLineSource.hpp ConsoleSink.hpp FileSink.hpp \
          ParserStage.hpp PredicateFilterStage.hpp ProjectionStage.hpp \
          Pipeline.hpp PipelineBuilder.hpp CollectingEmitter.hpp \
          StageFactory.hpp StageRegistry.hpp PipelineLoader.hpp

all: $(TARGET) $(LEGACY_TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

$(LEGACY_TARGET): $(TARGET)
	ln -sf $(TARGET) $(LEGACY_TARGET)

run: $(TARGET)
	./$(TARGET) --config configs/chaos-test.yaml

run-chaos: $(TARGET)
	./$(TARGET) --config configs/chaos-test.yaml
	./$(TARGET) --config configs/empty-file-test.yaml

run-configs: $(TARGET)
	./$(TARGET) --config configs/error-report.yaml
	./$(TARGET) --config configs/api-traffic.yaml
	./$(TARGET) --config configs/bandwidth-by-client.yaml
	./$(TARGET) --config configs/chaos-test.yaml

run-filtered: $(TARGET)
	@mkdir -p output
	./$(TARGET) data/access-small.log --min-status 400 --path-prefix /api --fields timestamp,clientIp,status --out output/filtered.log
	@cat output/filtered.log

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $(TEST_SRCS)

experiment: $(TARGET)
	@python3 scripts/run_experiment.py

coverage: clean
	@echo "Coverage derlemesi yapiliyor..."
	$(CXX) $(COVFLAGS) -o $(COV_TARGET) $(TEST_SRCS)
	@echo "Testler kosuluyor..."
	./$(COV_TARGET)
	@echo ""
	@echo "============================================================"
	@echo " GCOV SATIR KAPSAMA (LINE COVERAGE) RAPORU"
	@echo "============================================================"
	gcov -r $(COV_TARGET)-test_runner.cpp
	@echo "============================================================"

clean:
	rm -f $(TARGET) $(LEGACY_TARGET) $(TEST_TARGET) $(COV_TARGET)
	rm -f *.gcda *.gcno *.gcov tests/*.gcda tests/*.gcno
	rm -rf output/*.log

.PHONY: all run run-chaos run-configs run-filtered test experiment coverage clean
