SOURCES = $(wildcard ./*.cc)
OBJECTS = $(SOURCES:.cc=.o)
HEADERS = $(wildcard ./*.hh)

CCFLAGS += -fPIC

TARGET = libnet.a

%.o: %.cc $(HEADERS)
	$(CC) $(CCFLAGS) -c $< -o $@

all: $(TARGET)

$(TARGET): $(OBJECTS)
	ar rcs $(TARGET) $(OBJECTS)

clean:
	-rm -f *.o $(TARGET)