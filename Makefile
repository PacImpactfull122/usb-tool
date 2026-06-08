CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
TARGET   = usb-tool
SRCS     = main.cpp usb_linux.cpp

$(TARGET): $(SRCS) usb.h
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
