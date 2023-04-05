# MIT License

# Copyright (c) 2021 ByteDance Inc. All rights reserved.
# Copyright (c) 2021 Duke University. All rights reserved.

# See LICENSE for license information


# make clean; make for non-GDR version
# make clean; GDR=1 make for GDR version
name = control_check
objects = control_check.o
CC = g++

CFLAGS = -O3
LDFLAGS = -libverbs -lglog -lpthread -lgflags

$(name) : $(objects)
	g++ -o $(name) $(objects) $(LDFLAGS)
	rm -rf $(objects)

$(objects) : %.o : %.cpp $(headers)
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY : clean
clean:
	rm $(name)  $(objects) 
