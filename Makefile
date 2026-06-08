CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic
TARGET   = usbctl
SRCS     = main.cpp usb_linux.cpp descritor.cpp
PREFIX   = /usr/local

.PHONY: all debug install clean

all: $(TARGET)

$(TARGET): $(SRCS) usb.h
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

debug: CXXFLAGS = -std=c++17 -O0 -g -fsanitize=address,undefined -Wall -Wextra
debug: $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)
