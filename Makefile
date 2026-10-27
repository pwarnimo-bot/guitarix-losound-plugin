BUNDLE    := redsun.lv2
TARGET    := $(BUNDLE)/redsun.so
CXX       ?= g++
CXXFLAGS  ?= -O2 -march=x86-64 -mtune=generic
CXXFLAGS  += -std=c++17 -fPIC -fvisibility=hidden -Wall -Wextra $(shell pkg-config --cflags lv2)
LDFLAGS   += -shared
LV2DIR    ?= $(HOME)/.lv2

all: $(TARGET)

$(TARGET): src/redsun.cpp src/dsp.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test/render: test/render.cpp
	$(CXX) -std=c++17 -O2 -Wall $< -o $@ -ldl

test: $(TARGET) test/render
	./test/render ./$(TARGET) test/out

install: $(TARGET)
	mkdir -p $(LV2DIR)/$(BUNDLE)
	install -m 644 $(BUNDLE)/*.ttl $(LV2DIR)/$(BUNDLE)/
	install -m 755 $(TARGET) $(LV2DIR)/$(BUNDLE)/

uninstall:
	rm -rf $(LV2DIR)/$(BUNDLE)

clean:
	rm -f $(TARGET) test/render
	rm -rf test/out

.PHONY: all test install uninstall clean
