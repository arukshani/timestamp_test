### HW timestamps using UDP

```
sudo ./hw_server 5000
sudo ./hw_client 10.1.0.1 5000

hw_common.h
```

### HW timestamps using fake PTP SYNC

```
client -> fake_ptp
server -> fake_ptp_recv

fake_ptp.h
```

### memory to memory UDP latency with nic clock 
```
server.c
client.c
```

