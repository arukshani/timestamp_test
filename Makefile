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
TARGET = hw_client
# TARGET = hw_server
# TARGET = fake_ptp_v2
# TARGET = ptp_v2_recv
# TARGET = fake_ptp
# TARGET = fake_ptp_recv

default: $(TARGET)

clean:
	$(RM) count *.o *.gch *~ ../*.gch $(TARGET)
