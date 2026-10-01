CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -O1 -g
SRC = ../src/aruco_geom.c ../src/aruco_detect.c ../src/aruco_crop.c \
       test_aruco.c

test_aruco: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

run: test_aruco
	./test_aruco

clean:
	rm -rf test_aruco test_aruco.dSYM
