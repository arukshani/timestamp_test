CC = gcc

# CC flags:
#  -pthread links pthread library to the project
#  -g       adds debugging information to the executable file
#  -Wall    turns on most, but not all, CC warnings
OPTS = -pthread -g -Wall

# the build target executable:
# TARGET = udp_client
# TARGET = udp_server
# TARGET = client
# TARGET = server
# TARGET = hw_client
TARGET = hw_server

default: $(TARGET)

clean:
	$(RM) count *.o *.gch *~ ../*.gch $(TARGET)
