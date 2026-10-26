CXX      ?= g++
SRC       = src/agnes.cpp

CXXFLAGS  = -std=c++17 -O3 -flto -fno-exceptions -DCHESS_NO_EXCEPTIONS -DNDEBUG -DBACKENED_NO_VIRTUAL -Wall -pthread -Isrc -Isrc/tb
LDFLAGS   = -flto -pthread


ifeq ($(OS),Windows_NT)
  LDFLAGS += -Wl,--stack,16777216 -static
  EXE_EXT  = .exe
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -Wl,-stack_size,0x1000000
  endif
endif

fathom.o: src/tb/tbprobe.c src/tb/tbchess.c src/tb/tbprobe.h src/tb/tbconfig.h src/tb/stdendian.h
	gcc -std=c11 -O2 -Isrc -Isrc/tb -c src/tb/tbprobe.c -o fathom.o

.PHONY: all x86-64 avx2 bmi2 avx512 vnni native pgo pgo-clean clean

all: avx2

x86-64: fathom.o
	$(CXX) $(CXXFLAGS) -msse2 $(SRC) fathom.o -o agnes-x86-64$(EXE_EXT) $(LDFLAGS)

avx2: fathom.o
	$(CXX) $(CXXFLAGS) -mavx2 $(SRC) fathom.o -o agnes-avx2$(EXE_EXT) $(LDFLAGS)

bmi2: fathom.o
	$(CXX) $(CXXFLAGS) -mavx2 -mbmi2 -DBACKENED_USE_PEXT $(SRC) fathom.o -o agnes-bmi2$(EXE_EXT) $(LDFLAGS)

avx512: fathom.o
	$(CXX) $(CXXFLAGS) -mavx512f -mavx512bw -mavx512vl -mavx512vnni $(SRC) fathom.o -o agnes-avx512$(EXE_EXT) $(LDFLAGS)

vnni: fathom.o
	$(CXX) $(CXXFLAGS) -mavx2 -mavxvnni $(SRC) fathom.o -o agnes-vnni$(EXE_EXT) $(LDFLAGS)

native: fathom.o
	$(CXX) $(CXXFLAGS) -march=native $(SRC) fathom.o -o agnes-native$(EXE_EXT) $(LDFLAGS)

PGODIR := pgo-data

pgo: pgo-clean fathom.o
	mkdir -p $(PGODIR)
	$(CXX) $(CXXFLAGS) -march=native -fprofile-generate=$(PGODIR) $(SRC) fathom.o -o agnes-pgo-gen$(EXE_EXT) $(LDFLAGS)
	./agnes-pgo-gen$(EXE_EXT) bench 10 >/dev/null 2>&1 || true
	$(CXX) $(CXXFLAGS) -march=native -fprofile-use=$(PGODIR) -fprofile-correction $(SRC) fathom.o -o agnes-pgo$(EXE_EXT) $(LDFLAGS)
	@echo "PGO binary ready: ./agnes-pgo$(EXE_EXT)"

pgo-clean:
	rm -rf $(PGODIR) agnes-pgo-gen$(EXE_EXT) agnes-pgo$(EXE_EXT) *.gcda

clean:
	rm -f agnes-x86-64$(EXE_EXT) agnes-avx2$(EXE_EXT) agnes-bmi2$(EXE_EXT) \
	      agnes-avx512$(EXE_EXT) agnes-vnni$(EXE_EXT) agnes-native$(EXE_EXT) \
	      agnes-pgo-gen$(EXE_EXT) agnes-pgo$(EXE_EXT)
	rm -rf $(PGODIR) fathom.o
